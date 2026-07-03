// Maneuver scheduler/assist — KSP-style maneuver nodes for trajectory planning.
//
// A maneuver node represents a planned burn at a specific future time. The
// player can place nodes on their predicted trajectory, set the burn
// orientation (prograde, retrograde, normal, antinormal, radial in/out, or
// manual toward mouse), and optionally have the ship auto-execute the burn.
//
// Prediction integration: for every node k, the prediction sim spawns a
// phantom ghost ship that executes nodes 0..k at their scheduled times —
// regardless of the nodes' auto-execute flags — so the post-burn trajectory
// of the whole plan (and of every intermediate step) is always visible
// without the real ship doing anything. Ghost k's trajectory is drawn from
// node k onward; before that it coincides with the previous plan segment.
// Node k's marker also sits on ghost k's trajectory, which by construction
// passes through the node.
//
// The auto-execute setting on a node controls whether the REAL player ship
// also performs the burn when it reaches the node's time. The setting
// carries over as the default for newly placed nodes.

import { State, Triangle, pushMessage } from "./engine.js";
import { PI, TAU, degToRad, radToDeg, deltaAngle, deltaAngleRad, dst, dst2 } from "./math.js";

// ---------------------------------------------------------------------------
// Burn orientation modes
// ---------------------------------------------------------------------------

export const ManeuverOrientation = {
    Prograde: "prograde",
    Retrograde: "retrograde",
    Normal: "normal",
    Antinormal: "antinormal",
    RadialIn: "radial_in",
    RadialOut: "radial_out",
    Manual: "manual",
};

// Per-node accent colors (cycled). Also used for the ghost trajectory lines.
const NODE_COLORS = [
    [120, 200, 255],   // cyan
    [255, 170, 70],    // orange
    [190, 130, 255],   // purple
    [130, 255, 170],   // green
    [255, 120, 190],   // pink
    [255, 235, 100],   // yellow
];

export function maneuverColor(i) {
    return NODE_COLORS[i % NODE_COLORS.length];
}

// Autopilot tuning
const ALIGN_LEAD = 4.0;          // s before burn start the autopilot begins turning
const STEER_DEADBAND = 0.75;     // deg — no turn input inside this error band
const BURN_ANGLE_TOL = 15.0;     // deg — only thrust when pointed this close

let nextNodeId = 1;

// ---------------------------------------------------------------------------
// ManeuverNode
// ---------------------------------------------------------------------------

export class ManeuverNode {
    constructor(burnTime) {
        this.id = nextNodeId++;
        this.burnTime = burnTime;       // absolute globalTime at which burn happens
        this.dvMagnitude = 50.0;        // delta-V magnitude in m/s
        this.orientation = ManeuverOrientation.Prograde;
        this.manualAngle = 0.0;         // radians, used when orientation == Manual
        this.autoExecute = false;       // whether the real ship auto-executes this burn
        this.worldX = 0;               // world position (updated to track trajectory)
        this.worldY = 0;
        this.initialVelX = 0;          // ship velocity at this point (for burn heading)
        this.initialVelY = 0;
        // The reference body this node was created with. Prograde/retrograde/
        // radial directions are computed relative to this body's velocity and
        // position, not in absolute coordinates.
        this.refBodyId = null;         // entity ID of the ref body (null if system center)
        this.refBodyIsSystemCenter = false;
        // Cached ref body state at the node's trajectory step (updated during snap)
        this.refX = 0;
        this.refY = 0;
        this.refVelX = 0;
        this.refVelY = 0;
        // Frozen mode: node stays a constant time in the future. When frozen,
        // burnTime is updated each frame to currentTime + frozenOffset.
        this.frozen = false;
        this.frozenOffset = 0.0;

        // Transient state for auto-execution (not serialized)
        this.startVelX = 0;
        this.startVelY = 0;
        this.burnHeading = 0;
        this.burnDVApplied = 0;        // accumulated thrust delta-V (for completion check)
    }

    // Compute the burn heading (radians) at the given ship state.
    // Prograde/retrograde/normal/antinormal are computed relative to the
    // ref body's velocity (not absolute). Radial in/out use the ref body's
    // position. If refBody is null, falls back to absolute velocity.
    computeHeading(shipState, refBody) {
        const refVX = refBody ? (refBody.velX || 0) : 0;
        const refVY = refBody ? (refBody.velY || 0) : 0;
        const relVX = shipState.velX - refVX;
        const relVY = shipState.velY - refVY;
        switch (this.orientation) {
            case ManeuverOrientation.Prograde:
                return Math.atan2(relVY, relVX);
            case ManeuverOrientation.Retrograde:
                return Math.atan2(-relVY, -relVX);
            case ManeuverOrientation.Normal:
                // 90° CCW from prograde
                return Math.atan2(relVX, -relVY);
            case ManeuverOrientation.Antinormal:
                // 90° CW from prograde
                return Math.atan2(-relVX, relVY);
            case ManeuverOrientation.RadialIn:
                if (refBody) {
                    return Math.atan2(refBody.y - shipState.y, refBody.x - shipState.x);
                }
                return Math.atan2(relVY, relVX);
            case ManeuverOrientation.RadialOut:
                if (refBody) {
                    return Math.atan2(shipState.y - refBody.y, shipState.x - refBody.x);
                }
                return Math.atan2(-relVY, -relVX);
            case ManeuverOrientation.Manual:
                return this.manualAngle;
            default:
                return Math.atan2(relVY, relVX);
        }
    }

    serialize() {
        return {
            id: this.id,
            burnTime: this.burnTime,
            dvMagnitude: this.dvMagnitude,
            orientation: this.orientation,
            manualAngle: this.manualAngle,
            autoExecute: this.autoExecute,
            worldX: this.worldX,
            worldY: this.worldY,
            initialVelX: this.initialVelX,
            initialVelY: this.initialVelY,
            refBodyId: this.refBodyId,
            refBodyIsSystemCenter: this.refBodyIsSystemCenter,
            burnDVApplied: this.burnDVApplied,
            frozen: this.frozen,
            frozenOffset: this.frozenOffset,
        };
    }

    static deserialize(data) {
        const node = new ManeuverNode(data.burnTime);
        node.id = data.id;
        node.dvMagnitude = data.dvMagnitude;
        node.orientation = data.orientation;
        node.manualAngle = data.manualAngle;
        node.autoExecute = data.autoExecute;
        node.worldX = data.worldX || 0;
        node.worldY = data.worldY || 0;
        node.initialVelX = data.initialVelX || 0;
        node.initialVelY = data.initialVelY || 0;
        node.refBodyId = data.refBodyId ?? null;
        node.refBodyIsSystemCenter = data.refBodyIsSystemCenter ?? false;
        node.burnDVApplied = data.burnDVApplied ?? 0;
        node.frozen = data.frozen ?? false;
        node.frozenOffset = data.frozenOffset ?? 0;
        return node;
    }
}

// ---------------------------------------------------------------------------
// ManeuverScheduler
// ---------------------------------------------------------------------------

export class ManeuverScheduler {
    constructor() {
        this.nodes = [];
        this.defaultAutoExecute = false;
        this.activeManualNode = null;  // node currently being manually oriented
        this.activeBurnNode = null;    // node currently being auto-executed by real ship
    }

    addNode(node) {
        this.nodes.push(node);
        this.nodes.sort((a, b) => a.burnTime - b.burnTime);
    }

    removeNode(node) {
        const idx = this.nodes.indexOf(node);
        if (idx >= 0) this.nodes.splice(idx, 1);
        if (this.activeBurnNode === node) this.activeBurnNode = null;
        if (this.activeManualNode === node) this.activeManualNode = null;
    }

    clear() {
        this.nodes = [];
        this.activeManualNode = null;
        this.activeBurnNode = null;
    }

    getSortedNodes() {
        return this.nodes.slice().sort((a, b) => a.burnTime - b.burnTime);
    }

    // Remove nodes whose burn window has passed.
    // Manual nodes despawn shortly after their burn time (whether or not
    // the player actually performed the burn). Auto nodes are normally
    // removed by applyManeuverAutoExecute when the burn completes, but
    // we keep a fallback threshold in case the burn never started.
    cleanupPastNodes(currentTime, threshold = 120.0) {
        this.nodes = this.nodes.filter(n => {
            const burnDuration = n.dvMagnitude / Triangle.accel;
            if (n.autoExecute) {
                return n.burnTime > currentTime - threshold;
            } else {
                // Manual node: despawn 2 seconds after the burn window ends
                return currentTime < n.burnTime + burnDuration + 2.0;
            }
        });
    }

    serialize() {
        return {
            nodes: this.getSortedNodes().map(n => n.serialize()),
            defaultAutoExecute: this.defaultAutoExecute,
        };
    }
}

// ---------------------------------------------------------------------------
// Trajectory helpers
// ---------------------------------------------------------------------------

// Find the trajectory index whose DRAWN position (after applying the
// ref-body transform) is closest to the given draw-world position.
export function findClosestTrajectoryIndexDrawn(trajectory, refTrajectory, refX, refY, drawX, drawY) {
    if (!trajectory || trajectory.length === 0) return -1;
    let minDist = Infinity;
    let minIdx = -1;
    for (let i = 0; i < trajectory.length; i++) {
        let px, py;
        if (refTrajectory && i < refTrajectory.length) {
            px = refX + (trajectory[i].x - refTrajectory[i].x);
            py = refY + (trajectory[i].y - refTrajectory[i].y);
        } else {
            px = trajectory[i].x;
            py = trajectory[i].y;
        }
        const d = dst2(px - drawX, py - drawY);
        if (d < minDist) {
            minDist = d;
            minIdx = i;
        }
    }
    return minIdx;
}

// Estimate velocity at trajectory index `idx` by finite differencing.
// Returns { velX, velY }.
export function estimateVelocityAt(trajectory, idx, predictDelta) {
    if (idx > 0 && idx < trajectory.length - 1) {
        return {
            velX: (trajectory[idx + 1].x - trajectory[idx - 1].x) / (2 * predictDelta),
            velY: (trajectory[idx + 1].y - trajectory[idx - 1].y) / (2 * predictDelta),
        };
    } else if (idx < trajectory.length - 1) {
        return {
            velX: (trajectory[idx + 1].x - trajectory[idx].x) / predictDelta,
            velY: (trajectory[idx + 1].y - trajectory[idx].y) / predictDelta,
        };
    } else if (idx > 0) {
        return {
            velX: (trajectory[idx].x - trajectory[idx - 1].x) / predictDelta,
            velY: (trajectory[idx].y - trajectory[idx - 1].y) / predictDelta,
        };
    }
    return { velX: 0, velY: 0 };
}

// Find the ref body trajectory for a node (the body the node was created with).
// Returns the trajectory array, or null if not available.
export function findNodeRefTrajectory(node) {
    if (node.refBodyIsSystemCenter && State.systemCenter) {
        return State.systemCenter.trajectory;
    }
    if (node.refBodyId != null) {
        for (const e of State.updateGroup) {
            if (e.id === node.refBodyId) return e.trajectory;
        }
    }
    return null;
}

// Find the trajectory that node `k` should snap to: the ship's own trajectory
// for node 0, or the preceding node's ghost trajectory for subsequent nodes.
// Returns { trajectory, startIndex } where startIndex is the prediction step
// at which the trajectory begins recording (0 for the ship's own trajectory,
// or the ghost's trajectoryStartTime for ghost trajectories).
export function findNodeTrajectory(nodes, nodeIndex) {
    if (nodeIndex === 0) {
        const own = State.ownEntity;
        if (!own || !own.trajectory || own.trajectory.length === 0) return null;
        return { trajectory: own.trajectory, startIndex: 0 };
    }
    const prevNode = nodes[nodeIndex - 1];
    const rec = ghostRecordFor(prevNode.id);
    if (!rec || !rec.trajectory || rec.trajectory.length === 0) {
        // Fallback to own trajectory if ghost data isn't available yet
        const own = State.ownEntity;
        if (!own || !own.trajectory || own.trajectory.length === 0) return null;
        return { trajectory: own.trajectory, startIndex: 0 };
    }
    return { trajectory: rec.trajectory, startIndex: rec.startIndex || 0 };
}

// ---------------------------------------------------------------------------
// Ref body resolution
// ---------------------------------------------------------------------------

// Resolve the reference body entity for a maneuver node. Each node stores
// which ref body it was created with (by entity ID, or a flag for system
// center). This function looks up the entity in the current State.
export function resolveRefBody(node) {
    if (!node) return null;
    if (node.refBodyIsSystemCenter) return State.systemCenter;
    if (node.refBodyId == null) return State.trajectoryRef;
    for (const e of State.updateGroup) {
        if (e.id === node.refBodyId) return e;
    }
    return State.trajectoryRef;
}

// ---------------------------------------------------------------------------
// Prediction-side ghosts (per-node, with chaining)
// ---------------------------------------------------------------------------

let simGhosts = [];

// Spawn one phantom ghost per node. Ghost k executes nodes 0..k, so its
// trajectory shows the plan up to and including node k — even when the node
// is not set to auto-execute. Must be called while the prediction sim state
// is active (entities go into the sim's updateGroup).
export function spawnManeuverGhosts(serializedNodes, ownEntity) {
    simGhosts = [];
    if (!ownEntity || !serializedNodes || serializedNodes.length === 0) return simGhosts;
    const sorted = [...serializedNodes]
        .map(n => ManeuverNode.deserialize(n))
        .sort((a, b) => a.burnTime - b.burnTime);
    for (let k = 0; k < sorted.length; k++) {
        const ghost = new Triangle();
        const ghostId = ghost.id;
        Object.assign(ghost, ownEntity);
        ghost.id = ghostId;
        ghost.ghost = true;
        ghost.phantom = true;
        ghost.parent_id = ownEntity.id;
        ghost.target = null;
        ghost.trajectory = [];
        ghost.trajectoryStartTime = null;
        ghost.planNodes = sorted.slice(0, k + 1);
        ghost.maneuverNodeId = sorted[k].id;
        ghost.burnHeading = null;
        ghost.recording = false;
        simGhosts.push(ghost);
        State.simCleanupBuffer.push(ghost);
    }
    return simGhosts;
}

// Advance every maneuver ghost one prediction step. Ghost burns are
// idealized: during a burn window the ghost snaps its facing to the (live)
// burn heading and thrusts via the normal ship control path, so the imparted
// dV matches what the real autopilot achieves without simulating the coarse
// bang-bang steering at prediction timestep resolution.
export function stepManeuverGhosts(simTime, predictDelta) {
    if (simGhosts.length === 0) return;
    for (const ghost of simGhosts) {
        if (!ghost.active) continue;
        let burning = false;
        for (const node of ghost.planNodes) {
            const burnDuration = node.dvMagnitude / Triangle.accel;
            if (simTime >= node.burnTime && simTime < node.burnTime + burnDuration) {
                const refBody = resolveRefBody(node);
                const shipState = { x: ghost.x, y: ghost.y, velX: ghost.velX, velY: ghost.velY };
                const heading = node.computeHeading(shipState, refBody);
                ghost.rotation = heading * radToDeg;
                ghost.rotateVel = 0.0;
                const cont = {
                    forward: 1, backward: 0, turnleft: 0, turnright: 0,
                    boost: 0, slowrotate: 0, primaryfire: 0, secondaryfire: 0,
                };
                ghost.control(cont);
                if (node.id === ghost.maneuverNodeId && ghost.burnHeading === null) {
                    ghost.burnHeading = heading;
                }
                ghost.recording = true;
                burning = true;
                break;
            }
        }
        if (!burning && !ghost.recording) {
            // Before first burn: no controls (coast)
        }
    }
}

// Package ghost results for transfer back to the main thread.
export function collectManeuverGhosts(predStartTime, predictDelta) {
    const out = [];
    for (const ghost of simGhosts) {
        const node = ghost.planNodes[ghost.planNodes.length - 1];
        let startIndex = ghost.trajectoryStartTime;
        if (!isFinite(startIndex)) startIndex = 0;
        startIndex = Math.max(0, startIndex);
        out.push({
            nodeId: ghost.maneuverNodeId,
            startIndex,
            trajectory: ghost.trajectory,
            burnHeading: ghost.burnHeading,
        });
    }
    simGhosts = [];
    return out;
}

// Find the ghost record for a given node ID.
function ghostRecordFor(nodeId) {
    for (const g of State.maneuverGhostData) {
        if (g.nodeId === nodeId) return g;
    }
    return null;
}

// ---------------------------------------------------------------------------
// UI layout and hit-testing
// ---------------------------------------------------------------------------

export function getManeuverPanelPos(nodeScreenX, nodeScreenY, canvasW, canvasH) {
    const panelW = 128;
    const panelH = 96;  // 5 rows
    let px = nodeScreenX + 20;
    let py = nodeScreenY - panelH / 2;
    if (px + panelW > canvasW - 4) px = nodeScreenX - panelW - 20;
    if (px < 4) px = 4;
    if (py < 4) py = 4;
    if (py + panelH > canvasH - 4) py = canvasH - panelH - 4;
    return { x: px, y: py, w: panelW, h: panelH };
}

export function getManeuverButtons(node, panelX, panelY) {
    const btns = [];
    const w = 38, h = 16;
    // Row 1: orientation
    btns.push({ label: 'Pro',  action: 'orient', value: ManeuverOrientation.Prograde,   x: panelX + 4,  y: panelY + 4,  w, h });
    btns.push({ label: 'Ret',  action: 'orient', value: ManeuverOrientation.Retrograde, x: panelX + 44, y: panelY + 4,  w, h });
    btns.push({ label: 'Nrm',  action: 'orient', value: ManeuverOrientation.Normal,     x: panelX + 84, y: panelY + 4,  w, h });
    // Row 2: orientation
    btns.push({ label: 'Anti', action: 'orient', value: ManeuverOrientation.Antinormal, x: panelX + 4,  y: panelY + 22, w, h });
    btns.push({ label: 'RIn',  action: 'orient', value: ManeuverOrientation.RadialIn,   x: panelX + 44, y: panelY + 22, w, h });
    btns.push({ label: 'ROut', action: 'orient', value: ManeuverOrientation.RadialOut,  x: panelX + 84, y: panelY + 22, w, h });
    // Row 3: manual + Δv adjust
    btns.push({ label: 'Man',  action: 'manual',   x: panelX + 4,  y: panelY + 40, w, h });
    btns.push({ label: '-',    action: 'dv_down',  x: panelX + 44, y: panelY + 40, w, h });
    btns.push({ label: '+',    action: 'dv_up',    x: panelX + 84, y: panelY + 40, w, h });
    // Row 4: time adjust + freeze
    btns.push({ label: '-t',   action: 't_down',   x: panelX + 4,  y: panelY + 58, w, h });
    btns.push({ label: '+t',   action: 't_up',     x: panelX + 44, y: panelY + 58, w, h });
    btns.push({ label: node.frozen ? 'FRZ✓' : 'FRZ', action: 'toggle_freeze', x: panelX + 84, y: panelY + 58, w, h });
    // Row 5: auto-execute + delete
    btns.push({
        label: node.autoExecute ? 'Auto:ON' : 'Auto:OFF',
        action: 'toggle_auto',
        x: panelX + 4,  y: panelY + 76, w: 60, h,
    });
    btns.push({ label: 'Del', action: 'delete', x: panelX + 68, y: panelY + 76, w: 54, h });
    return btns;
}

export function hitTestManeuverButtons(scheduler, mouseState, panelPositions) {
    for (let i = 0; i < scheduler.nodes.length; i++) {
        const node = scheduler.nodes[i];
        const panel = panelPositions[i];
        if (!panel) continue;
        const btns = getManeuverButtons(node, panel.x, panel.y);
        for (const b of btns) {
            if (mouseState.x >= b.x && mouseState.x <= b.x + b.w &&
                mouseState.y >= b.y && mouseState.y <= b.y + b.h) {
                return { node, button: b };
            }
        }
    }
    return null;
}

// ---------------------------------------------------------------------------
// Real-ship execution (autopilot)
// ---------------------------------------------------------------------------

// Called once per frame from Game.step BEFORE controls are networked and
// applied. Prunes finished/expired nodes and, if the earliest node is set to
// auto-execute, writes steering + thrust into `controls`. Any player steering
// input overrides the autopilot for that frame.
export function updateManeuverExecution(scheduler, controls) {
    const nodes = scheduler.nodes;
    if (nodes.length === 0) return;
    const own = State.ownEntity;
    if (!own || own.type() !== 1 /* Entities.Triangle */) return;
    const now = State.globalTime;

    // Prune nodes whose burn window has fully passed.
    for (let i = 0; i < nodes.length; i++) {
        const burnDuration = nodes[i].dvMagnitude / Triangle.accel;
        if (now >= nodes[i].burnTime + burnDuration) {
            if (nodes[i].autoExecute && scheduler.activeBurnNode === nodes[i]) {
                pushMessage(`Maneuver #${nodes[i].id} executed.`);
            } else {
                pushMessage(`Maneuver #${nodes[i].id} expired.`);
            }
            scheduler.removeNode(nodes[i]);
            i--;
        }
    }
    if (nodes.length === 0) return;

    const node = nodes[0]; // nodes are kept sorted by burnTime
    if (!node.autoExecute) return;
    if (now < node.burnTime - ALIGN_LEAD) return;
    if (State.lockControls) return;

    // The pilot always wins: any manual steering input suspends the autopilot.
    if (controls.forward || controls.backward || controls.turnleft ||
        controls.turnright || controls.boost) {
        return;
    }

    const refBody = resolveRefBody(node);
    const headingDeg = node.computeHeading(own, refBody) * radToDeg;

    // Bang-bang steering with stopping-angle prediction (same idea as the
    // missile guidance). Braking angular decel is rotateSpeed * (1 + damping).
    const rs = Triangle.rotateSpeed * (controls.slowrotate ? Triangle.slowRotateSpeed : 1.0);
    const brake = rs * (1.0 + Triangle.rotateSlowSpeedMult);
    const stopAngle = own.rotateVel * Math.abs(own.rotateVel) / (2.0 * brake);
    const err = deltaAngle(own.rotation + stopAngle, headingDeg);
    if (err > STEER_DEADBAND) controls.turnright = 1;
    else if (err < -STEER_DEADBAND) controls.turnleft = 1;

    const trueErr = Math.abs(deltaAngle(own.rotation, headingDeg));
    if (now >= node.burnTime && trueErr < BURN_ANGLE_TOL) {
        controls.forward = 1;
        // Accumulate thrust delta-V for completion check
        node.burnDVApplied += Triangle.accel * State.delta;
        if (node.burnDVApplied >= node.dvMagnitude) {
            scheduler.removeNode(node);
            pushMessage(`Maneuver #${node.id} executed.`);
        }
    }
}
