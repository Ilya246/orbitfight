// Ported from src/main.cpp. Supports both single-player (authority) mode
// and multiplayer (connected to a C++ server via WebSocket-to-TCP proxy).

import {
    State, g_camera, Entity, Triangle, CelestialBody, Projectile, Missile,
    buildQuadtree, updateEntities, generateSystem, fullClear, setupShip,
    drawTrajectory, drawPolygon, idLookup, pushMessage, movementEqual,
} from "./engine.js";
import { MiscInfoUI, HelpUI, MenuUI, ChatPanel } from "./ui.js";
import { PI, TAU, degToRad, dst, dst2, deltaAngleRad } from "./math.js";
import { Entities } from "./types.js";
import { NetworkClient } from "./net.js";
import { startPrediction, pollPrediction, isPredictionRunning } from "./prediction.js";
import {
    ManeuverScheduler, ManeuverNode, ManeuverOrientation,
    findClosestTrajectoryIndexDrawn, resolveRefBody,
    updateManeuverExecution, maneuverColor,
    getManeuverPanelPos, getManeuverButtons,
    hitTestManeuverButtons,
} from "./maneuver.js";

const DBL_MAX = Number.MAX_VALUE;

export class Game {
    constructor(canvas) {
        this.canvas = canvas;
        const ctx = canvas.getContext("2d", { alpha: false });
        if (!ctx) throw new Error("2D canvas context not available");
        this.ctx = ctx;
        this.raf = 0;
        this.miscUI = new MiscInfoUI();
        this.helpUI = new HelpUI();
        this.menuUI = new MenuUI();
        this.chatPanel = new ChatPanel();
        this.netClient = null;

        this.keysDown = new Set();
        this.mouseDown = false;

        this.deltaClock = 0;
        this.lastShowFramerate = 0;

        this.loopError = null;
        this.errorCount = 0;
        this.wantConnect = false;

        this.maneuverScheduler = new ManeuverScheduler();
        State.maneuverScheduler = this.maneuverScheduler;
        this.maneuverNodeScreenPositions = [];
        this.maneuverPanelPositions = [];
        this.frozenPanels = new Map();
        this.hoveredManeuverNode = null;
        this.heldManeuverButton = null;
        this.heldButtonTimer = 0;

        this.handleResize();
    }

    start() {
        this.attachEvents();
        State.systemCenter = new CelestialBody(true);
        this.startFreeplay();
        this.autoFrameCamera();
        this.helpUI.show(8);
        
        const now = performance.now();
        this.deltaClock = now;
        this.lastShowFramerate = now;
        State.globalTime = 0.0;
        
        this.loop(now);
        // Check if we should auto-connect (non-localhost). If so, show the
        // username prompt dialog instead of connecting immediately.
        this.checkAutoConnect();
    }

    checkAutoConnect() {
        if (typeof window === "undefined" || !window.location) return;
        const hostname = window.location.hostname;
        if (!hostname || hostname === "localhost" || hostname === "127.0.0.1") {
            return; // dev mode — no auto-connect
        }
        // Show the username prompt dialog
        const savedName = localStorage.getItem("orbitfight_username") || "Player";
        const input = document.getElementById("username-input");
        if (input) input.value = savedName;
        const dialog = document.getElementById("username-dialog");
        if (dialog) dialog.style.display = "flex";
        this.autoConnectUrl = this.getAutoConnectUrl();
    }

    getAutoConnectUrl() {
        const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
        return `${proto}//${window.location.hostname}:7818/`;
    }

    async proceedWithAutoConnect(name) {
        if (!this.autoConnectUrl) return;
        State.name = name;
        pushMessage(`Connecting to ${this.autoConnectUrl}...`);
        try {
            await this.connectToServer(this.autoConnectUrl, name);
        } catch (e) {
            pushMessage("Auto-connect failed. Running in single-player mode.");
        }
    }

    cancelAutoConnect() {
        this.autoConnectUrl = null;
    }

    stop() {
        cancelAnimationFrame(this.raf);
        this.detachEvents();
    }

    startFreeplay() {
        fullClear(true);
        State.nextID = 0;
        State.kills = 0;
        State.systemCenter = new CelestialBody(true);
        generateSystem();
        const own = new Triangle();
        own.name = State.name;
        own.setColor(
            Math.floor(Math.random() * 128 + 80),
            Math.floor(Math.random() * 128 + 80),
            Math.floor(Math.random() * 128 + 80),
        );
        State.ownEntity = own;
        setupShip(own);
        State.authority = true;
        this.menuUI.active = false;
        this.autoSelectSystemCenter();
        this.maneuverScheduler.clear();
    }

    // By default, predict trajectories against the system center so the
    // player gets orbit previews without having to press Tab first. The
    // user can still override this with Tab (or unset by re-selecting the
    // same body), in which case we don't second-guess them.
    autoSelectSystemCenter() {
        if (State.systemCenter) {
            State.trajectoryRef = State.systemCenter;
        }
    }

    resetSystem() {
        this.maneuverScheduler.clear();
        State.simulating = false;
        State.simCleanupBuffer = [];
        State.ghostTrajectories = [];
        State.ghostTrajectoryStarts = [];
        State.ghostTrajectoryColors = [];
        State.quadtree = [];
        State.trajectoryRef = null;
        State.lastPredict = State.globalTime;
        State.lastSweep = State.globalTime;
        State.controls = {
            forward: 0, backward: 0, turnright: 0, turnleft: 0,
            boost: 0, slowrotate: 0, primaryfire: 0, secondaryfire: 0,
        };
        State.lastControls = { ...State.controls };

        fullClear(true);
        State.nextID = 0;
        State.kills = 0;
        State.systemCenter = new CelestialBody(true);
        generateSystem();
        const own = new Triangle();
        own.name = State.name;
        own.setColor(
            Math.floor(Math.random() * 128 + 80),
            Math.floor(Math.random() * 128 + 80),
            Math.floor(Math.random() * 128 + 80),
        );
        State.ownEntity = own;
        setupShip(own);
        this.autoFrameCamera();
        this.menuUI.active = false;
        pushMessage("System regenerated.");
        this.autoSelectSystemCenter();
    }

    async connectToServer(proxyUrl, name) {
        this.disconnectFromServer();
        pushMessage(`Connecting to ${proxyUrl}...`);
        const client = new NetworkClient({ proxyUrl, name });
        try {
            await client.connect();
            this.netClient = client;
            State.serverSocket = client;
            State.authority = false;
            fullClear(true);
            pushMessage(`Connected.`);
            this.menuUI.active = false;
            this.autoSelectSystemCenter();
        } catch (e) {
            pushMessage(`Could not connect: ${e?.message || e}`);
            this.netClient = null;
            State.serverSocket = null;
            throw e;
        }
    }

    disconnectFromServer() {
        if (this.netClient) {
            this.netClient.disconnect();
            this.netClient = null;
        }
        State.serverSocket = null;
        State.authority = true;
    }

    autoFrameCamera() {
        if (!State.ownEntity) return;
        let closest = null;
        let minD = Infinity;
        for (const e of State.updateGroup) {
            if (e.type() === Entities.CelestialBody) {
                const d = dst(e.x - State.ownEntity.x, e.y - State.ownEntity.y);
                if (d < minD) { minD = d; closest = e; }
            }
        }
        if (closest) {
            const target = minD / (g_camera.w / 3);
            g_camera.scale = Math.max(0.5, Math.min(target, 200));
        }
    }

    // -------------------------------------------------------------------------
    // Event wiring
    // -------------------------------------------------------------------------

    onKeyDown = (e) => {
        // Don't capture keys when typing in an input field (chat, dialogs)
        if (e.target && (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA")) {
            return;
        }
        if (["Tab", " ", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"].includes(e.key)) {
            e.preventDefault();
        }
        if (e.repeat) return;
        this.keysDown.add(e.code);

        const k = e.code;
        if (k === "Escape") {
            this.menuUI.active = !this.menuUI.active;
            return;
        }
        // Enter focuses chat input
        if (k === "Enter" || k === "NumpadEnter") {
            const chatInput = document.getElementById("chat-input");
            if (chatInput) {
                chatInput.focus();
                e.preventDefault();
            }
            return;
        }
        if (k === "KeyH") {
            if (this.helpUI.active) {
                this.helpUI.active = false;
            } else {
                this.helpUI.show(8);
            }
            return;
        }
        if (this.menuUI.active) return;

        if (k === "KeyT") {
            this.targetClosestToCursor();
        } else if (k === "Tab") {
            this.pickReferenceBody();
        } else if (k === "KeyQ" && State.ownEntity) {
            State.controls.slowrotate = State.controls.slowrotate ? 0 : 1;
        } else if (k === "KeyM") {
            this.placeManeuverNode();
        } else if (k === "KeyN" && e.shiftKey) {
            this.maneuverScheduler.clear();
            pushMessage("Cleared all maneuver nodes.");
        }
    };

    onKeyUp = (e) => {
        this.keysDown.delete(e.code);
    };

    onMouseMove = (e) => {
        const rect = this.canvas.getBoundingClientRect();
        State.mousePos.x = Math.floor(e.clientX - rect.left);
        State.mousePos.y = Math.floor(e.clientY - rect.top);
        // Update manual orientation for the active manual node
        if (this.maneuverScheduler.activeManualNode) {
            this.updateManualOrientation();
        }
    };

    onMouseDown = (e) => {
        this.onMouseMove(e);
        if (this.menuUI.active) {
            const hit = this.menuUI.hitTest(State.mousePos.x, State.mousePos.y);
            if (hit) this.onMenuButton(hit.label);
            return;
        }
        // Check maneuver node buttons first (using pre-computed panel positions)
        const hit = hitTestManeuverButtons(
            this.maneuverScheduler,
            State.mousePos,
            this.maneuverPanelPositions,
        );
        if (hit) {
            this.onManeuverButton(hit.node, hit.button);
            return;
        }
        this.mouseDown = true;
    };

    onMouseUp = () => {
        this.mouseDown = false;
        // Release manual orientation
        if (this.maneuverScheduler.activeManualNode) {
            this.maneuverScheduler.activeManualNode = null;
        }
        // Release held dv button
        this.heldManeuverButton = null;
    };

    onWheel = (e) => {
        if (this.menuUI.active) return;
        // If hovering over a maneuver node, adjust its delta-V magnitude
        if (this.hoveredManeuverNode) {
            e.preventDefault();
            const step = e.shiftKey ? 50 : 10;
            const dir = e.deltaY < 0 ? 1 : -1;
            this.hoveredManeuverNode.dvMagnitude = Math.max(0, this.hoveredManeuverNode.dvMagnitude + dir * step);
            return;
        }
        e.preventDefault();
        const factor = 1.0 + 0.1 * (e.deltaY < 0 ? -1 : 1);
        g_camera.zoom(factor);
    };

    onResize = () => this.handleResize();

    attachEvents() {
        window.addEventListener("keydown", this.onKeyDown, { passive: false });
        window.addEventListener("keyup", this.onKeyUp);
        this.canvas.addEventListener("mousemove", this.onMouseMove);
        this.canvas.addEventListener("mousedown", this.onMouseDown);
        window.addEventListener("mouseup", this.onMouseUp);
        this.canvas.addEventListener("wheel", this.onWheel, { passive: false });
        window.addEventListener("resize", this.onResize);
    }

    detachEvents() {
        window.removeEventListener("keydown", this.onKeyDown);
        window.removeEventListener("keyup", this.onKeyUp);
        this.canvas.removeEventListener("mousemove", this.onMouseMove);
        this.canvas.removeEventListener("mousedown", this.onMouseDown);
        window.removeEventListener("mouseup", this.onMouseUp);
        this.canvas.removeEventListener("wheel", this.onWheel);
        window.removeEventListener("resize", this.onResize);
    }

    onMenuButton(label) {
        switch (label) {
            case "Freeplay":
            this.startFreeplay();
            break;
            case "Resume":
            this.menuUI.active = false;
            break;
            case "Reset System":
            if (State.authority) {
                this.resetSystem();
            } else {
                pushMessage("Cannot reset while connected to a server.");
            }
            break;
            case "Connect to Server":
            this.wantConnect = true;
            this.menuUI.active = false;
            break;
            case "Disconnect":
            this.disconnectFromServer();
            this.startFreeplay();
            break;
            case "How to Play":
            this.helpUI.show(15);
            this.menuUI.active = false;
            break;
            case "Center View":
            if (State.ownEntity) {
                this.autoFrameCamera();
            }
            this.menuUI.active = false;
            break;
        }
    }

    handleResize() {
        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        const cssW = this.canvas.clientWidth || 800;
        const cssH = this.canvas.clientHeight || 800;
        this.canvas.width = Math.floor(cssW * dpr);
        this.canvas.height = Math.floor(cssH * dpr);
        this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        g_camera.resize(cssW, cssH);
    }

    // -------------------------------------------------------------------------
    // Game actions
    // -------------------------------------------------------------------------

    targetClosestToCursor() {
        if (!State.ownEntity || State.ownEntity.type() !== Entities.Triangle) return;
        let minDst = DBL_MAX;
        let closest = null;
        for (const e of State.updateGroup) {
            if (e === State.ownEntity) continue;
            const d = dst2(
                e.x - State.ownX - (State.mousePos.x - g_camera.w * 0.5) * g_camera.scale,
                e.y - State.ownY - (State.mousePos.y - g_camera.h * 0.5) * g_camera.scale,
            ) - e.radius * e.radius;
            if (d < minDst) { minDst = d; closest = e; }
        }
        const tri = State.ownEntity;
        const unset = closest === tri.target;
        tri.target = unset ? null : closest;
        if (this.netClient) {
            this.netClient.sendSetTarget(unset ? null : (closest ? closest.id : null));
        }
    }

    pickReferenceBody() {
        let minDst = DBL_MAX;
        let closest = null;
        for (const e of State.updateGroup) {
            const d = dst2(
                e.x - State.ownX - (State.mousePos.x - g_camera.w * 0.5) * g_camera.scale,
                e.y - State.ownY - (State.mousePos.y - g_camera.h * 0.5) * g_camera.scale,
            ) - e.radius * e.radius;
            if (d < minDst) { minDst = d; closest = e; }
        }
        if (State.systemCenter) {
            const dCenter = dst2(
                State.systemCenter.x - State.ownX - (State.mousePos.x - g_camera.w * 0.5) * g_camera.scale,
                State.systemCenter.y - State.ownY - (State.mousePos.y - g_camera.h * 0.5) * g_camera.scale,
            );
            if (dCenter < minDst) closest = State.systemCenter;
        }
        if (closest === State.trajectoryRef) {
            State.trajectoryRef = null;
        } else if (closest) {
            State.trajectoryRef = closest;
            pushMessage(`Selected entity id ${closest.id} as reference body`);
        }
    }

    // -------------------------------------------------------------------------
    // Maneuver scheduler actions
    // -------------------------------------------------------------------------

    // Place a maneuver node on the trajectory point closest to the mouse.
    // Requires trajectory prediction to be active (State.trajectoryRef set).
    placeManeuverNode() {
        if (!State.trajectoryRef) {
            pushMessage("Press Tab near a body first to enable trajectory prediction.");
            return;
        }
        if (!State.ownEntity || !State.ownEntity.trajectory || State.ownEntity.trajectory.length === 0) {
            pushMessage("No trajectory available yet — wait for prediction.");
            return;
        }
        // Convert mouse to draw-world coordinates. The trajectory is drawn
        // relative to State.trajectoryRef (see drawTrajectory in engine.js),
        // so we must search using the drawn (ref-relative) position, not the
        // absolute world position. Otherwise the wrong trajectory point is
        // selected when a non-system-center ref body is chosen.
        const drawX = State.ownX + (State.mousePos.x - g_camera.w * 0.5) * g_camera.scale;
        const drawY = State.ownY + (State.mousePos.y - g_camera.h * 0.5) * g_camera.scale;
        const traj = State.ownEntity.trajectory;
        const ref = State.trajectoryRef;
        const refTraj = ref ? ref.trajectory : null;
        const idx = findClosestTrajectoryIndexDrawn(
            traj, refTraj,
            ref ? ref.x : 0, ref ? ref.y : 0,
            drawX, drawY,
        );
        if (idx < 0) {
            pushMessage("Could not find a trajectory point near cursor.");
            return;
        }
        // burnTime = current time + (idx+1) * predictDelta
        const burnTime = State.globalTime + (idx + 1) * State.predictDelta;
        const node = new ManeuverNode(burnTime);
        node.autoExecute = this.maneuverScheduler.defaultAutoExecute;
        // Anchor the node to a fixed world position so it doesn't teleport
        // as the trajectory is recomputed each prediction cycle.
        node.worldX = traj[idx].x;
        node.worldY = traj[idx].y;
        // Store the ship's velocity at this trajectory point for a stable
        // burn-direction arrow.
        if (idx > 0 && idx < traj.length - 1) {
            node.initialVelX = (traj[idx + 1].x - traj[idx - 1].x) / (2 * State.predictDelta);
            node.initialVelY = (traj[idx + 1].y - traj[idx - 1].y) / (2 * State.predictDelta);
        } else if (idx < traj.length - 1) {
            node.initialVelX = (traj[idx + 1].x - traj[idx].x) / State.predictDelta;
            node.initialVelY = (traj[idx + 1].y - traj[idx].y) / State.predictDelta;
        } else if (idx > 0) {
            node.initialVelX = (traj[idx].x - traj[idx - 1].x) / State.predictDelta;
            node.initialVelY = (traj[idx].y - traj[idx - 1].y) / State.predictDelta;
        }
        // Remember which ref body this node was created with, so that
        // prograde/retrograde/radial directions are computed relative to
        // the correct body even if the player later switches ref bodies.
        if (State.trajectoryRef === State.systemCenter) {
            node.refBodyIsSystemCenter = true;
            node.refBodyId = null;
        } else if (State.trajectoryRef) {
            node.refBodyIsSystemCenter = false;
            node.refBodyId = State.trajectoryRef.id;
        }
        this.maneuverScheduler.addNode(node);
        pushMessage(`Placed maneuver node #${node.id} (Δv ${node.dvMagnitude}, ${node.orientation}).`);
    }

    // Handle a click on a maneuver node button.
    onManeuverButton(node, button) {
        switch (button.action) {
            case 'orient':
                node.orientation = button.value;
                break;
            case 'manual':
                // Start manual orientation — node follows mouse until released
                this.maneuverScheduler.activeManualNode = node;
                node.orientation = ManeuverOrientation.Manual;
                this.updateManualOrientation();
                break;
            case 'dv_up':
                node.dvMagnitude += 10;
                this.startHoldingButton(node, button.action);
                break;
            case 'dv_down':
                node.dvMagnitude = Math.max(0, node.dvMagnitude - 10);
                this.startHoldingButton(node, button.action);
                break;
            case 'dv_up_large':
                node.dvMagnitude += 50;
                this.startHoldingButton(node, button.action);
                break;
            case 'dv_down_large':
                node.dvMagnitude = Math.max(0, node.dvMagnitude - 50);
                this.startHoldingButton(node, button.action);
                break;
            case 'toggle_auto':
                node.autoExecute = !node.autoExecute;
                this.maneuverScheduler.defaultAutoExecute = node.autoExecute;
                pushMessage(`Auto-execute ${node.autoExecute ? 'ON' : 'OFF'} (default for new nodes).`);
                break;
            case 'delete':
                this.maneuverScheduler.removeNode(node);
                break;
        }
    }

    // Start holding a dv button for repeat adjustment.
    startHoldingButton(node, action) {
        this.heldManeuverButton = { node, action };
        this.heldButtonTimer = 0;
    }

    // Process held button repeat (called from step).
    processHeldManeuverButton(dt) {
        if (!this.heldManeuverButton) return;
        const { node, action } = this.heldManeuverButton;
        // Stop if node was deleted
        if (!this.maneuverScheduler.nodes.includes(node)) {
            this.heldManeuverButton = null;
            return;
        }
        this.heldButtonTimer += dt * 1000; // ms
        if (this.heldButtonTimer >= 80) {
            this.heldButtonTimer = 0;
            switch (action) {
                case 'dv_up':         node.dvMagnitude += 10; break;
                case 'dv_down':       node.dvMagnitude = Math.max(0, node.dvMagnitude - 10); break;
                case 'dv_up_large':   node.dvMagnitude += 50; break;
                case 'dv_down_large': node.dvMagnitude = Math.max(0, node.dvMagnitude - 50); break;
            }
        }
    }

    // Update the manual orientation angle based on mouse position relative
    // to the active manual node's screen position.
    updateManualOrientation() {
        const node = this.maneuverScheduler.activeManualNode;
        if (!node) return;
        // Find the node's screen position
        const idx = this.maneuverScheduler.nodes.indexOf(node);
        if (idx < 0 || !this.maneuverNodeScreenPositions[idx]) return;
        const pos = this.maneuverNodeScreenPositions[idx];
        const dx = State.mousePos.x - pos.x;
        const dy = State.mousePos.y - pos.y;
        if (dx === 0 && dy === 0) return;
        node.manualAngle = Math.atan2(dy, dx);
    }

    readKeyboardIntoControls() {
        if (this.menuUI.active) {
            State.controls.forward = 0;
            State.controls.backward = 0;
            State.controls.turnleft = 0;
            State.controls.turnright = 0;
            State.controls.boost = 0;
            State.controls.primaryfire = 0;
            State.controls.secondaryfire = 0;
            return;
        }
        const k = this.keysDown;
        State.controls.forward = k.has("KeyW") ? 1 : 0;
        State.controls.backward = k.has("KeyS") ? 1 : 0;
        State.controls.turnleft = k.has("KeyA") ? 1 : 0;
        State.controls.turnright = k.has("KeyD") ? 1 : 0;
        State.controls.boost = k.has("KeyE") ? 1 : 0;
        State.controls.primaryfire = k.has("Space") ? 1 : 0;
        State.controls.secondaryfire = k.has("KeyX") ? 1 : 0;
    }

    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------

    loop = (now) => {
        this.raf = requestAnimationFrame(this.loop);
        try {
            this.readKeyboardIntoControls();
            this.step();
            this.render();
        } catch (err) {
            const msg = err instanceof Error ? (err.stack || err.message) : String(err);
            console.error("Orbitfight loop error:", err);
            this.loopError = msg;
            this.errorCount = (this.errorCount || 0) + 1;
        }
    };

    step() {
        // Apply maneuver auto-execute BEFORE controls are sent to server,
        // so that burn overrides are communicated in multiplayer too.
        // updateManeuverExecution also prunes finished/expired nodes.
        updateManeuverExecution(this.maneuverScheduler, State.controls);
        // Clean up nodes whose burn time is far in the past (missed burns).
        this.maneuverScheduler.cleanupPastNodes(State.globalTime);

        // Process held maneuver dv buttons (hold-to-repeat)
        this.processHeldManeuverButton(State.delta);

        const online = !!this.netClient && State.serverSocket !== null;

        if (online) {
            if (!movementEqual(State.controls, State.lastControls)) {
                this.netClient.sendControls(State.controls);
                State.lastControls = { ...State.controls };
            }
        }

        if (State.authority && State.lastSweep + State.projectileSweepSpacing < State.globalTime) {
            for (const e of State.updateGroup) {
                if (e.type() !== Entities.Projectile && e.type() !== Entities.Missile) continue;
                let closest = DBL_MAX;
                if (State.ownEntity) {
                    closest = Math.min(closest, dst2(e.x - State.ownEntity.x, e.y - State.ownEntity.y));
                }
                if (closest > State.sweepThreshold) e.active = false;
            }
            State.lastSweep = State.globalTime;
        }

        const deleted = [];
        for (let i = 0; i < State.updateGroup.length; i++) {
            if (!State.updateGroup[i].active) {
                deleted.push(State.updateGroup[i]);
                State.updateGroup.splice(i, 1);
                i--;
            }
        }
        for (const d of deleted) {
            for (const e of State.updateGroup) e.onEntityDelete(d);
            if (d === State.trajectoryRef) {
                // The body the user was predicting against is gone. Fall
                // back to the system center so the trajectory view is not
                // lost — the user can still Tab to pick something else or
                // unset it.
                State.trajectoryRef = null;
                this.autoSelectSystemCenter();
            }
            if (d === State.ownEntity) {
                // The plan was anchored to the old ship's orbit.
                this.maneuverScheduler.clear();
                State.maneuverGhostData = [];
                if (State.authority) {
                    const tri = new Triangle();
                    tri.name = State.name;
                    tri.setColor(
                        Math.floor(Math.random() * 128 + 80),
                        Math.floor(Math.random() * 128 + 80),
                        Math.floor(Math.random() * 128 + 80),
                    );
                    State.ownEntity = tri;
                    setupShip(tri);
                } else {
                    State.ownEntity = null;
                }
            }
        }

        // Prediction runs continuously in a Web Worker (or time-sliced on
        // the main thread as fallback). Each frame we:
        //   1. Poll for completed predictions and apply results.
        //   2. If no prediction is running, start a new one immediately.
        // This replaces the old predictSpacing timer — prediction is always
        // running, but on a separate thread so it doesn't cause stutter.
        if (pollPrediction()) {
            // A prediction just completed; trajectories are now updated.
        }
        if (!isPredictionRunning() && State.trajectoryRef) {
            startPrediction();
        }

        buildQuadtree();
        updateEntities();

        if (State.updateGroup.length > 0) {
            let x = 0, y = 0, tmass = 0;
            for (const e of State.updateGroup) {
                x += e.x * e.mass;
                y += e.y * e.mass;
                tmass += e.mass;
            }
            if (tmass !== 0 && State.systemCenter) {
                x /= tmass;
                y /= tmass;
                State.systemCenter.setPosition(x, y);
            }
        }

        if (State.ownEntity) {
            if (State.lockControls) {
                State.ownEntity.control({
                    forward: 0, backward: 0, turnright: 0, turnleft: 0,
                    boost: 0, slowrotate: 0, primaryfire: 0, secondaryfire: 0,
                });
            } else {
                State.ownEntity.control(State.controls);
            }
        }

        const now = performance.now();
        let dt = (now - this.deltaClock) / 1000.0;
        this.deltaClock = now;

        State.measureFrames++;
        if (now - this.lastShowFramerate >= 1000.0) {
            State.framerate = State.measureFrames;
            State.measureFrames = 0;
            this.lastShowFramerate = now;
        }

        if (State.deltaOverride > 0.0) {
            dt = State.deltaOverride;
        } else {
            dt *= State.timescale;
        }
        
        dt = Math.min(dt, 1.0 / 15.0);
        State.delta = dt;
        State.globalTime += dt;
    }

    // Prediction is now handled by the prediction coordinator (worker-based).
    // See step() for the polling/starting logic. This method is kept for
    // any external callers but is a no-op.
    runPrediction() {
        if (!isPredictionRunning() && State.trajectoryRef) {
            startPrediction();
        }
    }

    // -------------------------------------------------------------------------
    // Render
    // -------------------------------------------------------------------------

    render() {
            const ctx = this.ctx;
            const W = g_camera.w;
            const H = g_camera.h;
            ctx.fillStyle = `rgb(${Math.floor(State.worldBrightness / 2)},0,${Math.floor(State.worldBrightness)})`;
            ctx.fillRect(0, 0, W, H);

            if (State.ownEntity) {
                State.ownX = State.ownEntity.x;
                State.ownY = State.ownEntity.y;
                State.drawShiftX = -State.ownX;
                State.drawShiftY = -State.ownY;
            }

            // === World pass ===
            ctx.save();
            ctx.translate(W * 0.5, H * 0.5);
            ctx.scale(1 / g_camera.scale, 1 / g_camera.scale);

            for (let i = 0; i < State.ghostTrajectories.length; i++) {
                drawTrajectory(ctx, State.ghostTrajectoryColors[i], State.ghostTrajectories[i], State.ghostTrajectoryStarts[i]);
            }

            // Maneuver plan: each node's post-burn trajectory, drawn from the
            // node onward in that node's accent color.
            if (State.maneuverGhostData.length > 0 && State.maneuverScheduler) {
                const nodes = State.maneuverScheduler.nodes;
                for (let i = 0; i < nodes.length; i++) {
                    const rec = State.maneuverGhostData.find(g => g.nodeId === nodes[i].id);
                    if (!rec || !rec.trajectory || rec.trajectory.length === 0) continue;
                    const color = maneuverColor(i);
                    drawTrajectory(ctx, color, rec.trajectory, rec.startIndex || 0);
                }
            }

            for (const e of State.updateGroup) {
                e.draw(ctx);
            }

            ctx.restore();

            // === UI / overlay pass ===
            const scale = g_camera.scale;
            const w2sX = (wx) => W * 0.5 + (wx - State.ownX) / scale;
            const w2sY = (wy) => H * 0.5 + (wy - State.ownY) / scale;

            if (State.trajectoryRef) {
                const r = Math.max(5, State.trajectoryRef.radius / scale);
                drawPolygon(ctx, w2sX(State.trajectoryRef.x), w2sY(State.trajectoryRef.y), r, 4, 0, null, "rgba(255,255,64,1)", 1);
            }
            if (State.ownEntity && State.ownEntity.target) {
                const target = State.ownEntity.target;
                const r = Math.max(5, target.radius / scale);
                drawPolygon(ctx, w2sX(target.x), w2sY(target.y), r, 3, 0, null, "rgba(255,0,0,1)", 1);
            }

            for (const e of State.updateGroup) {
                const screenRadius = e.radius / scale;
                const uiX = w2sX(e.x);
                const uiY = w2sY(e.y);
                if (uiX < -40 || uiX > W + 40 || uiY < -40 || uiY > H + 40) continue;

                if (screenRadius < 3) {
                    let iconRadius = 2;
                    let iconColor = `rgb(${e.color[0]},${e.color[1]},${e.color[2]})`;
                    if (e.type() === Entities.CelestialBody) {
                        if (e.blackhole) {
                            drawPolygon(ctx, uiX, uiY, 4, 8, 0, null, "rgba(255,40,40,0.9)", 1.5);
                            continue;
                        }
                        if (e.star) {
                            iconRadius = 3;
                        } else {
                            iconRadius = 2;
                        }
                    } else if (e.type() === Entities.Triangle) {
                        iconRadius = 3;
                    } else if (e.type() === Entities.Missile) {
                        drawPolygon(ctx, uiX, uiY, 3, 3, e.rotation * degToRad, "rgba(255,0,0,0.9)", null, 0);
                        if (State.ownEntity && e.target === State.ownEntity) {
                            drawPolygon(ctx, uiX, uiY, 6, 4, Math.PI / 4, null, "rgba(255,0,0,1)", 1);
                        }
                        continue;
                    } else if (e.type() === Entities.Projectile) {
                        iconRadius = 2;
                        iconColor = "rgba(255,80,80,0.9)";
                    }
                    ctx.fillStyle = iconColor;
                    ctx.beginPath();
                    ctx.arc(uiX, uiY, iconRadius, 0, TAU);
                    ctx.fill();
                }

                if (e.type() === Entities.Triangle && e !== State.ownEntity) {
                    if (e.name) {
                        ctx.font = `10px ui-monospace, SFMono-Regular, "Menlo", monospace`;
                        ctx.fillStyle = "rgba(255,255,255,0.85)";
                        ctx.textAlign = "center";
                        ctx.textBaseline = "middle";
                        ctx.fillText(e.name, uiX, uiY - 14);
                    }
                }

                if (e.type() === Entities.CelestialBody && e.blackhole && e !== State.trajectoryRef) {
                    drawPolygon(ctx, uiX, uiY, 5, 4, 0, null, "rgba(255,0,0,0.8)", 1);
                }
            }
            ctx.textAlign = "left";
            ctx.textBaseline = "top";

            // HUD: reload / boost / secondary bars
            if (State.ownEntity && State.ownEntity.type() === Entities.Triangle && !this.menuUI.active) {
                const t = State.ownEntity;
                const reloadW = (-t.reloadProgress / Triangle.reload + 1.0) * 40;
                const secW = (-t.secondaryCharge / Triangle.secondaryStockpile + 1.0) * 40;
                const boostW = (-t.boostProgress / Triangle.boostCooldown + 1.0) * 40;
                if (reloadW > 0) {
                    ctx.fillStyle = "rgba(255,64,64,0.95)";
                    ctx.fillRect(W * 0.5 - reloadW / 2, H * 0.5 + 40, reloadW, 4);
                }
                if (secW > 0) {
                    ctx.fillStyle = "rgba(255,64,255,0.95)";
                    ctx.fillRect(W * 0.5 - secW / 2, H * 0.5 + 46, secW, 4);
                }
                if (boostW > 0) {
                    ctx.fillStyle = "rgba(64,255,64,0.95)";
                    ctx.fillRect(W * 0.5 - boostW / 2, H * 0.5 - 40, boostW, 4);
                }
                let fwdColor = "rgba(255,255,255,0.95)";
                if (State.controls.slowrotate) fwdColor = "rgba(255,255,0,0.95)";
                else if (State.controls.forward) fwdColor = "rgba(255,196,0,0.95)";
                else if (State.controls.backward) fwdColor = "rgba(255,64,64,0.95)";
                const rotRad = t.rotation * degToRad;
                ctx.fillStyle = fwdColor;
                ctx.beginPath();
                ctx.arc(W * 0.5 + 14 * Math.cos(rotRad), H * 0.5 + 14 * Math.sin(rotRad), 3, 0, TAU);
                ctx.fill();
            }

            // Maneuver nodes and their button panels
            this.drawManeuverUI(ctx, W, H, scale, w2sX, w2sY);

            this.miscUI.update(ctx);
            this.chatPanel.update(ctx);
            this.helpUI.update(ctx);
            if (this.menuUI.active) this.menuUI.update(ctx);

            if (!State.trajectoryRef && !this.menuUI.active && !this.helpUI.active) {
                ctx.font = `11px ui-monospace, SFMono-Regular, "Menlo", monospace`;
                ctx.fillStyle = "rgba(255,255,255,0.45)";
                ctx.textAlign = "center";
                ctx.textBaseline = "bottom";
                ctx.fillText("Press Tab near a body to predict trajectories", W * 0.5, H - 12);
                ctx.textAlign = "left";
                ctx.textBaseline = "top";
            } else if (State.trajectoryRef === State.systemCenter && !this.menuUI.active && !this.helpUI.active) {
                ctx.font = `11px ui-monospace, SFMono-Regular, "Menlo", monospace`;
                ctx.fillStyle = "rgba(255,255,255,0.45)";
                ctx.textAlign = "center";
                ctx.textBaseline = "bottom";
                ctx.fillText("Reference: system center · Tab to change", W * 0.5, H - 12);
                ctx.textAlign = "left";
                ctx.textBaseline = "top";
            }

            if (this.loopError) {
                ctx.fillStyle = "rgba(80,0,0,0.92)";
                ctx.fillRect(20, 20, W - 40, 120);
                ctx.strokeStyle = "rgba(255,0,0,1)";
                ctx.lineWidth = 2;
                ctx.strokeRect(20, 20, W - 40, 120);
                ctx.fillStyle = "#ffaaaa";
                ctx.font = `12px ui-monospace, SFMono-Regular, "Menlo", monospace`;
                ctx.textAlign = "left";
                ctx.textBaseline = "top";
                this.wrapText(ctx, "Loop error: " + this.loopError, 30, 30, W - 60);
            }
        }

        // Check whether the mouse cursor is inside a panel rectangle.
        isMouseInPanel(panel) {
            return State.mousePos.x >= panel.x && State.mousePos.x <= panel.x + panel.w &&
                   State.mousePos.y >= panel.y && State.mousePos.y <= panel.y + panel.h;
        }

        // Draw maneuver nodes on the trajectory and their button panels.
        drawManeuverUI(ctx, W, H, scale, w2sX, w2sY) {
            const nodes = this.maneuverScheduler.nodes;
            this.hoveredManeuverNode = null;

            // Clean up frozen panels for deleted nodes
            const nodeIds = new Set(nodes.map(n => n.id));
            for (const id of this.frozenPanels.keys()) {
                if (!nodeIds.has(id)) this.frozenPanels.delete(id);
            }

            // Snap each node to the trajectory point matching its burnTime.
            // We find the index by TIME (not spatial proximity): as the ship
            // moves forward, the trajectory moves with it, so the same world
            // position stays at the same index. Using time-based indexing
            // ensures the index decreases as currentTime advances, so the
            // node actually approaches the present and gets executed.
            // When the node's time is in the past, clamp the index to 0 so
            // the marker follows the player's current position instead of
            // floating off. Skip nodes that are being actively burned.
            const traj = State.ownEntity ? State.ownEntity.trajectory : null;
            const predictDelta = State.predictDelta;
            const currentTime = State.globalTime;
            // The ref body used for DRAWING position (aligns marker with the
            // trajectory line, which is drawn relative to State.trajectoryRef).
            const drawRef = State.trajectoryRef;
            const drawRefTraj = drawRef ? drawRef.trajectory : null;
            if (traj && traj.length > 0) {
                for (const node of nodes) {
                    if (node === this.maneuverScheduler.activeBurnNode) continue;
                    // Find trajectory index by time: step i corresponds to
                    // globalTime = currentTime + (i+1) * predictDelta
                    let idx = Math.round((node.burnTime - currentTime) / predictDelta) - 1;
                    // Clamp to valid range. When in the past (idx < 0),
                    // clamp to 0 so the marker follows the player.
                    if (idx < 0) idx = 0;
                    if (idx >= traj.length) continue;
                    node.worldX = traj[idx].x;
                    node.worldY = traj[idx].y;
                    // burnTime stays as-is — it's the source of truth
                    // Update stored ship velocity for burn heading
                    if (idx > 0 && idx < traj.length - 1) {
                        node.initialVelX = (traj[idx + 1].x - traj[idx - 1].x) / (2 * predictDelta);
                        node.initialVelY = (traj[idx + 1].y - traj[idx - 1].y) / (2 * predictDelta);
                    } else if (idx < traj.length - 1) {
                        node.initialVelX = (traj[idx + 1].x - traj[idx].x) / predictDelta;
                        node.initialVelY = (traj[idx + 1].y - traj[idx].y) / predictDelta;
                    } else if (idx > 0) {
                        node.initialVelX = (traj[idx].x - traj[idx - 1].x) / predictDelta;
                        node.initialVelY = (traj[idx].y - traj[idx - 1].y) / predictDelta;
                    }
                    // Update the node's OWN ref body state (for burn heading).
                    let nodeRefTraj = null;
                    if (node.refBodyIsSystemCenter && State.systemCenter) {
                        nodeRefTraj = State.systemCenter.trajectory;
                    } else if (node.refBodyId != null) {
                        for (const e of State.updateGroup) {
                            if (e.id === node.refBodyId) { nodeRefTraj = e.trajectory; break; }
                        }
                    }
                    if (nodeRefTraj && idx < nodeRefTraj.length) {
                        node.refX = nodeRefTraj[idx].x;
                        node.refY = nodeRefTraj[idx].y;
                        if (idx > 0 && idx < nodeRefTraj.length - 1) {
                            node.refVelX = (nodeRefTraj[idx + 1].x - nodeRefTraj[idx - 1].x) / (2 * predictDelta);
                            node.refVelY = (nodeRefTraj[idx + 1].y - nodeRefTraj[idx - 1].y) / (2 * predictDelta);
                        } else if (idx < nodeRefTraj.length - 1) {
                            node.refVelX = (nodeRefTraj[idx + 1].x - nodeRefTraj[idx].x) / predictDelta;
                            node.refVelY = (nodeRefTraj[idx + 1].y - nodeRefTraj[idx].y) / predictDelta;
                        } else if (idx > 0) {
                            node.refVelX = (nodeRefTraj[idx].x - nodeRefTraj[idx - 1].x) / predictDelta;
                            node.refVelY = (nodeRefTraj[idx].y - nodeRefTraj[idx - 1].y) / predictDelta;
                        }
                    }
                }
            }

            // ---- Pass 1: compute screen positions and panel positions ----
            // The trajectory line is drawn relative to State.trajectoryRef
            // (see drawTrajectory in engine.js). To make the node marker
            // align with the line, we apply the same ref-body transform:
            //   screen_world = ref.x + (node.worldX - refTraj[idx].x)
            const screenPositions = [];
            const panelPositions = [];

            for (let i = 0; i < nodes.length; i++) {
                const node = nodes[i];
                let drawWorldX = node.worldX;
                let drawWorldY = node.worldY;
                if (drawRef && drawRefTraj && drawRefTraj.length > 0) {
                    let idx = Math.round((node.burnTime - currentTime) / predictDelta) - 1;
                    if (idx < 0) idx = 0;
                    if (idx >= 0 && idx < drawRefTraj.length) {
                        drawWorldX = drawRef.x + (node.worldX - drawRefTraj[idx].x);
                        drawWorldY = drawRef.y + (node.worldY - drawRefTraj[idx].y);
                    }
                }
                const sx = w2sX(drawWorldX);
                const sy = w2sY(drawWorldY);
                screenPositions.push({ x: sx, y: sy });

                // Check frozen panel: if the mouse is still inside the
                // frozen rectangle, keep using it so the panel doesn't
                // slide out from under the cursor.
                const frozen = this.frozenPanels.get(node.id);
                let panel;
                if (frozen && this.isMouseInPanel(frozen)) {
                    panel = frozen;
                } else {
                    panel = getManeuverPanelPos(sx, sy, W, H);
                    this.frozenPanels.delete(node.id);
                }
                panelPositions.push(panel);
            }

            // ---- Pass 2: freeze panels that the mouse just entered ----
            for (let i = 0; i < nodes.length; i++) {
                const node = nodes[i];
                if (this.frozenPanels.has(node.id)) continue;
                const panel = panelPositions[i];
                if (this.isMouseInPanel(panel)) {
                    this.frozenPanels.set(node.id, { ...panel });
                }
            }

            this.maneuverNodeScreenPositions = screenPositions;
            this.maneuverPanelPositions = panelPositions;

            // ---- Pass 3: draw everything ----

            for (let i = 0; i < nodes.length; i++) {
                const node = nodes[i];
                const pos = screenPositions[i];
                if (!pos) continue;
                const sx = pos.x;
                const sy = pos.y;

                // Check if mouse is hovering near this node (for scroll-wheel Δv)
                const mouseDist = dst(State.mousePos.x - sx, State.mousePos.y - sy);
                if (mouseDist < 20) {
                    this.hoveredManeuverNode = node;
                }

                // Per-node accent color
                const color = maneuverColor(i);
                const rgba = (a) => `rgba(${color[0]},${color[1]},${color[2]},${a})`;

                // Draw the burn direction arrow using the stored ship and ref
                // body velocity at the node's trajectory step.
                const shipState = { x: node.worldX, y: node.worldY, velX: node.initialVelX, velY: node.initialVelY };
                const refState = { x: node.refX, y: node.refY, velX: node.refVelX, velY: node.refVelY };
                const heading = node.computeHeading(shipState, refState);
                const hx = Math.cos(heading);
                const hy = Math.sin(heading);
                const arrowLen = Math.min(30, 10 + node.dvMagnitude * 0.3);
                const ax = sx + hx * arrowLen;
                const ay = sy + hy * arrowLen;
                ctx.strokeStyle = rgba(0.95);
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.moveTo(sx + hx * 8, sy + hy * 8);
                ctx.lineTo(ax, ay);
                ctx.stroke();
                // Arrowhead
                const headAngle = PI * 0.82;
                ctx.beginPath();
                ctx.moveTo(ax, ay);
                ctx.lineTo(ax + Math.cos(heading + headAngle) * 7, ay + Math.sin(heading + headAngle) * 7);
                ctx.moveTo(ax, ay);
                ctx.lineTo(ax + Math.cos(heading - headAngle) * 7, ay + Math.sin(heading - headAngle) * 7);
                ctx.stroke();

                // Ghost-ship glyph at the node, facing the burn heading
                drawPolygon(ctx, sx, sy, 11, 3, heading, null, rgba(0.5), 1.2);

                // Node marker — a small circle
                ctx.fillStyle = rgba(0.9);
                ctx.strokeStyle = rgba(1.0);
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.arc(sx, sy, 6, 0, TAU);
                ctx.fill();
                ctx.stroke();

                // Node label with T-minus countdown
                const dt = node.burnTime - currentTime;
                const tLabel = dt >= 0 ? `T-${dt.toFixed(1)}s` : `T+${(-dt).toFixed(1)}s`;
                const modeLabel = {
                    [ManeuverOrientation.Prograde]: "PRO",
                    [ManeuverOrientation.Retrograde]: "RET",
                    [ManeuverOrientation.Normal]: "NRM",
                    [ManeuverOrientation.Antinormal]: "ANTI",
                    [ManeuverOrientation.RadialIn]: "RIN",
                    [ManeuverOrientation.RadialOut]: "ROUT",
                    [ManeuverOrientation.Manual]: "MAN",
                }[node.orientation] || "?";
                const label = `${tLabel} · ${modeLabel} Δv${Math.round(node.dvMagnitude)}${node.autoExecute ? " · auto" : ""}`;
                ctx.font = `10px ui-monospace, SFMono-Regular, "Menlo", monospace`;
                const tw = ctx.measureText(label).width;
                ctx.fillStyle = "rgba(0,0,0,0.55)";
                ctx.fillRect(sx + 10, sy - 22, tw + 8, 14);
                ctx.fillStyle = rgba(1.0);
                ctx.textAlign = "left";
                ctx.textBaseline = "middle";
                ctx.fillText(label, sx + 14, sy - 15);
                ctx.textAlign = "left";
                ctx.textBaseline = "top";

                // Draw the button panel (using frozen or natural position)
                const panel = panelPositions[i];
                ctx.fillStyle = "rgba(15,15,25,0.88)";
                ctx.strokeStyle = `rgba(${color[0]},${color[1]},${color[2]},0.7)`;
                ctx.lineWidth = 1;
                ctx.fillRect(panel.x, panel.y, panel.w, panel.h);
                ctx.strokeRect(panel.x, panel.y, panel.w, panel.h);

                const btns = getManeuverButtons(node, panel.x, panel.y);
                for (const b of btns) {
                    const isActive = (b.action === 'orient' && b.value === node.orientation);
                    const isHovered = (State.mousePos.x >= b.x && State.mousePos.x <= b.x + b.w &&
                                       State.mousePos.y >= b.y && State.mousePos.y <= b.y + b.h);
                    if (isActive) {
                        ctx.fillStyle = rgba(0.85);
                    } else if (isHovered) {
                        ctx.fillStyle = "rgba(80,80,110,0.9)";
                    } else {
                        ctx.fillStyle = "rgba(50,50,70,0.9)";
                    }
                    ctx.strokeStyle = `rgba(${color[0]},${color[1]},${color[2]},0.6)`;
                    ctx.lineWidth = 1;
                    ctx.fillRect(b.x, b.y, b.w, b.h);
                    ctx.strokeRect(b.x, b.y, b.w, b.h);
                    ctx.fillStyle = isActive ? "rgba(10,10,16,0.95)" : "rgba(255,255,255,0.95)";
                    ctx.font = `9px ui-monospace, SFMono-Regular, "Menlo", monospace`;
                    ctx.textAlign = "center";
                    ctx.textBaseline = "middle";
                    ctx.fillText(b.label, b.x + b.w / 2, b.y + b.h / 2 + 0.5);
                }
                ctx.textAlign = "left";
                ctx.textBaseline = "top";
            }

            // Draw active burn indicator
            if (this.maneuverScheduler.activeBurnNode) {
                const node = this.maneuverScheduler.activeBurnNode;
                const idx = nodes.indexOf(node);
                if (idx >= 0 && screenPositions[idx]) {
                    const pos = screenPositions[idx];
                    const c = maneuverColor(idx);
                    ctx.strokeStyle = `rgba(${c[0]},${c[1]},${c[2]},0.8)`;
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    ctx.arc(pos.x, pos.y, 10, 0, TAU);
                    ctx.stroke();
                }
            }
        }

        wrapText(ctx, text, x, y, maxWidth) {
            const words = text.split(/\s+/);
            let line = "";
            let yy = y;
            for (const w of words) {
                const test = line ? line + " " + w : w;
                if (ctx.measureText(test).width > maxWidth && line) {
                    ctx.fillText(line, x, yy);
                    line = w;
                    yy += 16;
                    if (yy > y + 100) return;
                } else {
                    line = test;
                }
            }
            if (line) ctx.fillText(line, x, yy);
        }
    }
