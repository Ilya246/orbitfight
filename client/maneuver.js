// Maneuver scheduler/assist — KSP-style maneuver nodes for trajectory planning.
//
// A maneuver node represents a planned burn at a specific future time. The
// player can place nodes on their predicted trajectory, set the burn
// orientation (prograde, retrograde, normal, antinormal, radial in/out, or
// manual toward mouse), and optionally have the ship auto-execute the burn.
//
// During trajectory prediction, two ghost ships are spawned per maneuver
// chain:
//   - Ghost A ("preview"): applies an instant delta-V at each node, showing
//     the resulting post-burn trajectory. This shows what would happen if
//     the burn is executed perfectly.
//   - Ghost B ("executing"): physically turns and thrusts to perform the
//     burns, demonstrating that they are achievable. This ghost runs
//     regardless of the auto-execute setting.
//
// The auto-execute setting on a node controls whether the REAL player ship
// also performs the burn when it reaches the node's time. The setting
// carries over as the default for newly placed nodes.

import { State, Triangle } from "./engine.js";
import { PI, TAU, degToRad, deltaAngleRad, dst, dst2 } from "./math.js";

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

export const ORIENTATION_LABELS = {
    [ManeuverOrientation.Prograde]: "Pro",
    [ManeuverOrientation.Retrograde]: "Ret",
    [ManeuverOrientation.Normal]: "Nrm",
    [ManeuverOrientation.Antinormal]: "Anti",
    [ManeuverOrientation.RadialIn]: "RIn",
    [ManeuverOrientation.RadialOut]: "ROut",
    [ManeuverOrientation.Manual]: "Man",
};

export const ORIENTATION_ORDER = [
    ManeuverOrientation.Prograde,
    ManeuverOrientation.Retrograde,
    ManeuverOrientation.Normal,
    ManeuverOrientation.Antinormal,
    ManeuverOrientation.RadialIn,
    ManeuverOrientation.RadialOut,
    ManeuverOrientation.Manual,
];

// Distinct colors for maneuver ghost trajectories.
export const GHOST_PREVIEW_COLOR = [120, 200, 255];   // cyan — instant delta-V
export const GHOST_EXECUTING_COLOR = [120, 255, 170]; // green — physical burn

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

    // Compute the delta-V vector (dvX, dvY) at the given ship state.
    computeDeltaV(shipState, refBody) {
        const heading = this.computeHeading(shipState, refBody);
        return {
            x: this.dvMagnitude * Math.cos(heading),
            y: this.dvMagnitude * Math.sin(heading),
        };
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

// Find the trajectory index closest to the given world position.
export function findClosestTrajectoryIndex(trajectory, worldX, worldY) {
    if (!trajectory || trajectory.length === 0) return -1;
    let minDist = Infinity;
    let minIdx = -1;
    for (let i = 0; i < trajectory.length; i++) {
        const d = dst2(trajectory[i].x - worldX, trajectory[i].y - worldY);
        if (d < minDist) {
            minDist = d;
            minIdx = i;
        }
    }
    return minIdx;
}

// Find the trajectory index whose DRAWN position (after applying the
// ref-body transform) is closest to the given draw-world position.
// The trajectory is drawn relative to a ref body:
//   drawX = refX + (traj[i].x - refTraj[i].x)
//   drawY = refY + (traj[i].y - refTraj[i].y)
// This is used for node placement: the user clicks at a screen position,
// which corresponds to a draw-world position, and we need to find which
// trajectory point is drawn there. Using absolute coordinates would be
// wrong when a non-system-center ref body is selected.
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

// Compute the trajectory step index for a maneuver node.
// Returns -1 if the node is in the past or beyond the trajectory.
export function nodeStepIndex(node, currentTime, predictDelta) {
    const stepIndex = Math.round((node.burnTime - currentTime) / predictDelta) - 1;
    return stepIndex;
}

// Get the world position of a maneuver node on the current trajectory.
export function getManeuverNodePosition(node, trajectory, currentTime, predictDelta) {
    if (!trajectory || trajectory.length === 0) return null;
    const stepIndex = nodeStepIndex(node, currentTime, predictDelta);
    if (stepIndex < 0 || stepIndex >= trajectory.length) return null;
    return trajectory[stepIndex];
}

// Estimate the ship's state (position + velocity) at a maneuver node's position.
// Velocity is estimated by finite differencing the trajectory.
export function getShipStateAtNode(node, trajectory, currentTime, predictDelta) {
    if (!trajectory || trajectory.length === 0) return null;
    const stepIndex = nodeStepIndex(node, currentTime, predictDelta);
    if (stepIndex < 0 || stepIndex >= trajectory.length) return null;
    const pos = trajectory[stepIndex];
    let velX = 0, velY = 0;
    if (stepIndex > 0 && stepIndex < trajectory.length - 1) {
        velX = (trajectory[stepIndex + 1].x - trajectory[stepIndex - 1].x) / (2 * predictDelta);
        velY = (trajectory[stepIndex + 1].y - trajectory[stepIndex - 1].y) / (2 * predictDelta);
    } else if (stepIndex < trajectory.length - 1) {
        velX = (trajectory[stepIndex + 1].x - trajectory[stepIndex].x) / predictDelta;
        velY = (trajectory[stepIndex + 1].y - trajectory[stepIndex].y) / predictDelta;
    } else if (stepIndex > 0) {
        velX = (trajectory[stepIndex].x - trajectory[stepIndex - 1].x) / predictDelta;
        velY = (trajectory[stepIndex].y - trajectory[stepIndex - 1].y) / predictDelta;
    }
    return { x: pos.x, y: pos.y, velX, velY };
}

// Get the reference body's position at a maneuver node's burn time.
export function getRefBodyStateAtNode(node, refTrajectory, currentTime, predictDelta) {
    if (!refTrajectory || refTrajectory.length === 0) return null;
    const stepIndex = nodeStepIndex(node, currentTime, predictDelta);
    if (stepIndex < 0 || stepIndex >= refTrajectory.length) return null;
    return { x: refTrajectory[stepIndex].x, y: refTrajectory[stepIndex].y };
}

// ---------------------------------------------------------------------------
// Ghost ship management for prediction
// ---------------------------------------------------------------------------

// Resolve the reference body entity for a maneuver node. Each node stores
// which ref body it was created with (by entity ID, or a flag for system
// center). This function looks up the entity in the current State — which
// may be the real game state or the prediction sim's state.
export function resolveRefBody(node) {
    if (!node) return null;
    if (node.refBodyIsSystemCenter) return State.systemCenter;
    if (node.refBodyId == null) return State.trajectoryRef;
    // Linear search (updateGroup may not be sorted by ID in prediction sim)
    for (const e of State.updateGroup) {
        if (e.id === node.refBodyId) return e;
    }
    return State.trajectoryRef;
}

// Create a ghost ship for maneuver prediction.
// type: 'preview' (instant delta-V) or 'executing' (physical burn)
// nodes: array of serialized maneuver node data (plain objects)
export function createManeuverGhost(ownEntity, serializedNodes) {
    const ghost = new Triangle();
    const ghostId = ghost.id;
    Object.assign(ghost, ownEntity);
    ghost.id = ghostId;
    ghost.ghost = true;
    ghost.parent_id = ownEntity.id;
    ghost.trajectory = [];
    // Deserialize nodes into ManeuverNode instances
    ghost.maneuverQueue = serializedNodes
        .map(n => ManeuverNode.deserialize(n))
        .sort((a, b) => a.burnTime - b.burnTime);
    ghost.currentBurn = null;
    ghost.burnHeading = 0;
    ghost.burnDVApplied = serializedNodes[0].burnDVApplied;
    ghost.recording = false;
    ghost.color = [...GHOST_EXECUTING_COLOR];
    State.simCleanupBuffer.push(ghost);
    return ghost;
}

// Update a maneuver ghost during one prediction step.
//   ghost: the ghost entity
//   stepIndex: current step index (0-based)
//   startGlobalTime: the global time at the start of prediction
//   predictDelta: time per step
//   controls: the real ship's controls (ghost follows these before burn)
// This is called AFTER updateEntities() and BEFORE trajectory recording.
//
// The ghost only starts recording its trajectory when the first burn begins,
// so it visually "spawns" at the maneuver node rather than overlapping the
// real ship's trajectory from the current time.
//
// Each node's ref body is resolved per-node via resolveRefBody(), so
// prograde/retrograde/radial directions use the correct reference body.
export function updateManeuverGhost(ghost, stepIndex, startGlobalTime, predictDelta, controls) {
    const stepGlobalTime = startGlobalTime + (stepIndex + 1) * predictDelta;

    // Physical burn: turn toward heading, then thrust until delta-V achieved.
    if (!ghost.currentBurn && ghost.maneuverQueue.length > 0) {
        const node = ghost.maneuverQueue[0];
        const burnDuration = node.dvMagnitude / Triangle.accel;
        const burnStartTime = node.burnTime - burnDuration / 2;
        if (stepGlobalTime >= burnStartTime) {
            const shipState = { x: ghost.x, y: ghost.y, velX: ghost.velX, velY: ghost.velY };
            const refBody = resolveRefBody(node);
            ghost.currentBurn = node;
            ghost.burnHeading = node.computeHeading(shipState, refBody);
            if (burnStartTime > State.globalTime)
                ghost.burnDVApplied = 0;
            ghost.recording = true;
            ghost.maneuverQueue.shift();
        }
    }

    if (ghost.currentBurn) {
        const node = ghost.currentBurn;
        const angleDiff = deltaAngleRad(ghost.rotation * degToRad, ghost.burnHeading);
        const burnControls = {
            forward: 0, backward: 0, turnleft: 0, turnright: 0,
            boost: 0, slowrotate: 0, primaryfire: 0, secondaryfire: 0,
        };
        if (Math.abs(angleDiff) > 0.02) {
            if (angleDiff > 0) burnControls.turnright = 1;
            else burnControls.turnleft = 1;
        } else {
            burnControls.forward = 1;
            // Accumulate actual thrust delta-V for completion check.
            // This accounts for rotation time and is not affected by gravity.
            ghost.burnDVApplied += Triangle.accel * predictDelta;
        }
        ghost.control(burnControls);
        if (ghost.burnDVApplied >= node.dvMagnitude) {
            ghost.currentBurn = null;
        }
    } else if (!ghost.recording && controls) {
        // Before first burn: follow real ship controls (minus weapons)
        ghost.control({
            forward: controls.forward, backward: controls.backward,
            turnleft: controls.turnleft, turnright: controls.turnright,
            boost: controls.boost, slowrotate: controls.slowrotate,
            primaryfire: 0, secondaryfire: 0,
        });
    }
}

// ---------------------------------------------------------------------------
// UI layout and hit-testing
// ---------------------------------------------------------------------------

// Compute the panel position for a maneuver node, in screen coordinates.
// The panel is placed to the right of the node, or to the left if there
// isn't enough space on the right.
export function getManeuverPanelPos(nodeScreenX, nodeScreenY, canvasW, canvasH) {
    const panelW = 128;
    const panelH = 78;
    let px = nodeScreenX + 20;
    let py = nodeScreenY - panelH / 2;
    if (px + panelW > canvasW - 4) px = nodeScreenX - panelW - 20;
    if (px < 4) px = 4;
    if (py < 4) py = 4;
    if (py + panelH > canvasH - 4) py = canvasH - panelH - 4;
    return { x: px, y: py, w: panelW, h: panelH };
}

// Compute button rectangles for a maneuver node's UI panel.
// Returns an array of { label, action, value, x, y, w, h } objects.
export function getManeuverButtons(node, panelX, panelY) {
    const btns = [];
    const w = 38, h = 16;
    // Orientation row 1
    btns.push({ label: 'Pro',  action: 'orient', value: ManeuverOrientation.Prograde,   x: panelX + 4,  y: panelY + 4,  w, h });
    btns.push({ label: 'Ret',  action: 'orient', value: ManeuverOrientation.Retrograde, x: panelX + 44, y: panelY + 4,  w, h });
    btns.push({ label: 'Nrm',  action: 'orient', value: ManeuverOrientation.Normal,     x: panelX + 84, y: panelY + 4,  w, h });
    // Orientation row 2
    btns.push({ label: 'Anti', action: 'orient', value: ManeuverOrientation.Antinormal, x: panelX + 4,  y: panelY + 22, w, h });
    btns.push({ label: 'RIn',  action: 'orient', value: ManeuverOrientation.RadialIn,   x: panelX + 44, y: panelY + 22, w, h });
    btns.push({ label: 'ROut', action: 'orient', value: ManeuverOrientation.RadialOut,  x: panelX + 84, y: panelY + 22, w, h });
    // Manual + 4 magnitude adjust buttons (--, -, +, ++)
    btns.push({ label: 'Man',  action: 'manual',     x: panelX + 4,  y: panelY + 40, w, h });
    btns.push({ label: '--',   action: 'dv_down_large', x: panelX + 44, y: panelY + 40, w: 18, h });
    btns.push({ label: '-',    action: 'dv_down',       x: panelX + 64, y: panelY + 40, w: 18, h });
    btns.push({ label: '+',    action: 'dv_up',         x: panelX + 84, y: panelY + 40, w: 18, h });
    btns.push({ label: '++',   action: 'dv_up_large',   x: panelX + 104,y: panelY + 40, w: 18, h });
    // Auto-execute toggle + delete
    btns.push({
        label: node.autoExecute ? 'Auto:ON' : 'Auto:OFF',
        action: 'toggle_auto',
        x: panelX + 4,  y: panelY + 58, w: 60, h,
    });
    btns.push({ label: 'Del', action: 'delete', x: panelX + 68, y: panelY + 58, w: 54, h });
    return btns;
}

// Hit-test a mouse position against maneuver node buttons.
// Takes pre-computed panel positions (which may be frozen) so that
// hit-testing matches what the user sees on screen.
// Returns { node, button } if hit, or null.
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
