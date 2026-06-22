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
import { runPrediction as runPredictionImpl } from "./prediction.js";

const DBL_MAX = Number.MAX_VALUE;

export class Game {
    constructor(canvas) {
        this.canvas = canvas;
        const ctx = canvas.getContext("2d", { alpha: false });
        if (!ctx) throw new Error("2D canvas context not available");
        this.ctx = ctx;
        this.raf = 0;
        this.lastFrameTime = 0;
        this.miscUI = new MiscInfoUI();
        this.helpUI = new HelpUI();
        this.menuUI = new MenuUI();
        this.chatPanel = new ChatPanel();
        this.netClient = null;

        this.keysDown = new Set();
        this.mouseDown = false;

        this.deltaClock = 0;
        this.globalClock = 0;
        this.actualDeltaClock = 0;
        this.lastShowFramerate = 0;

        this.loopError = null;
        this.errorCount = 0;
        this.wantConnect = false;

        this.handleResize();
    }

    start() {
        this.attachEvents();
        State.systemCenter = new CelestialBody(true);
        this.startFreeplay();
        this.autoFrameCamera();
        this.helpUI.show(8);
        this.lastFrameTime = performance.now();
        this.globalClock = this.lastFrameTime;
        this.actualDeltaClock = this.lastFrameTime;
        this.deltaClock = this.lastFrameTime;
        this.loop(this.lastFrameTime);
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
        State.simulating = false;
        State.simCleanupBuffer = [];
        State.ghostTrajectories = [];
        State.ghostTrajectoryColors = [];
        State.quadtree = [];
        State.trajectoryRef = null;
        State.lastTrajectoryRef = null;
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
        }
    };

    onKeyUp = (e) => {
        this.keysDown.delete(e.code);
    };

    onMouseMove = (e) => {
        const rect = this.canvas.getBoundingClientRect();
        State.mousePos.x = Math.floor(e.clientX - rect.left);
        State.mousePos.y = Math.floor(e.clientY - rect.top);
    };

    onMouseDown = (e) => {
        this.onMouseMove(e);
        if (this.menuUI.active) {
            const hit = this.menuUI.hitTest(State.mousePos.x, State.mousePos.y);
            if (hit) this.onMenuButton(hit.label);
            return;
        }
        this.mouseDown = true;
    };

    onMouseUp = () => {
        this.mouseDown = false;
    };

    onWheel = (e) => {
        if (this.menuUI.active) return;
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
            State.lastTrajectoryRef = null;
        } else if (closest) {
            State.trajectoryRef = closest;
            pushMessage(`Selected entity id ${closest.id} as reference body`);
        }
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
            if (d === State.lastTrajectoryRef) State.lastTrajectoryRef = null;
            if (d === State.trajectoryRef) {
                // The body the user was predicting against is gone. Fall
                // back to the system center so the trajectory view is not
                // lost — the user can still Tab to pick something else or
                // unset it.
                State.trajectoryRef = null;
                this.autoSelectSystemCenter();
            }
            if (d === State.ownEntity) {
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

        if (State.globalTime - State.lastPredict > State.predictSpacing && State.trajectoryRef) {
            this.runPrediction();
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
                x /= State.updateGroup.length * tmass;
                y /= State.updateGroup.length * tmass;
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
        State.delta = (now - this.deltaClock) / 1000.0;
        this.deltaClock = now;
        State.measureFrames++;
        if (State.globalTime > this.lastShowFramerate + 1.0) {
            this.lastShowFramerate = State.globalTime;
            State.framerate = State.measureFrames;
            State.measureFrames = 0;
        }
        const actualDelta = (now - this.actualDeltaClock) / 1000.0;
        this.actualDeltaClock = now;
        if (State.deltaOverride > 0.0) State.delta = State.deltaOverride;
        else State.delta *= State.timescale;
        State.delta = Math.min(State.delta, 1.0 / 15.0);
        State.globalTime = (now - this.globalClock) / 1000.0;
    }

    runPrediction() {
        runPredictionImpl();
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
                drawTrajectory(ctx, State.ghostTrajectoryColors[i], State.ghostTrajectories[i]);
            }

            for (const e of State.updateGroup) {
                e.draw(ctx);
            }

            ctx.restore();

            // === UI / overlay pass ===
            const scale = g_camera.scale;
            const w2sX = (wx) => W * 0.5 + (wx - State.ownX) / scale;
            const w2sY = (wy) => H * 0.5 + (wy - State.ownY) / scale;

            if (State.lastTrajectoryRef) {
                const r = Math.max(5, State.lastTrajectoryRef.radius / scale);
                drawPolygon(ctx, w2sX(State.lastTrajectoryRef.x), w2sY(State.lastTrajectoryRef.y), r, 4, 0, null, "rgba(255,255,64,1)", 1);
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

                if (e.type() === Entities.CelestialBody && e.blackhole && e !== State.lastTrajectoryRef) {
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
