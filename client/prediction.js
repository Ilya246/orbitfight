// Trajectory prediction coordinator.
//
// Manages prediction in a Web Worker to avoid stutter on the main thread.
// The worker runs the full prediction loop and posts back trajectories in
// world coordinates. The main thread applies them to entities for rendering.
//
// If Web Workers are unavailable, falls back to time-sliced main-thread
// prediction (runs a few steps per frame to keep CPU load bounded).
//
// Trajectories are stored in WORLD coordinates — the ref-body transform is
// applied only at draw time (in drawTrajectory). This means trajectories
// don't need to be recomputed when the ref-body changes.

import {
    State, Triangle, CelestialBody, Projectile, Missile,
    g_camera,
} from "./engine.js";
import { Entities } from "./types.js";

// ---------------------------------------------------------------------------
// State serialization
// ---------------------------------------------------------------------------

// Serialize the current game state for transfer to the worker.
// We send plain objects (no methods) — the worker reconstructs entities.
export function serializeState() {
    const entities = [];
    for (const e of State.updateGroup) {
        const s = {
            id: e.id,
            entityType: e.type(),
            x: e.x, y: e.y,
            velX: e.velX, velY: e.velY,
            aX: e.aX, aY: e.aY,
            rotation: e.rotation, rotateVel: e.rotateVel,
            mass: e.mass, radius: e.radius,
            color: [...e.color],
            active: e.active,
            gravitates: e.gravitates,
            parent_id: e.parent_id,
            star: e.star,
            blackhole: e.blackhole,
        };
        // Triangle-specific fields
        if (e instanceof Triangle) {
            s.name = e.name;
            s.boostProgress = e.boostProgress;
            s.reloadProgress = e.reloadProgress;
            s.secondaryCharge = e.secondaryCharge;
            s.secondaryProgress = e.secondaryProgress;
            s.targetId = e.target ? e.target.id : null;
        }
        // Missile-specific fields
        if (e instanceof Missile) {
            s.fuel = e.fuel;
            s.prevItime = e.prevItime;
            s.thrust = e.thrust;
            s.targetId = e.target ? e.target.id : null;
            s.ownerId = e.owner ? e.owner.id : null;
        }
        entities.push(s);
    }

    return {
        entities,
        nextID: State.nextID,
        globalTime: State.globalTime,
        predictDelta: State.predictDelta,
        predictSteps: State.predictSteps,
        controls: { ...State.controls },
        ownEntityId: State.ownEntity ? State.ownEntity.id : null,
        trajectoryRefId: State.trajectoryRef ? State.trajectoryRef.id : null,
        hasSystemCenter: !!State.systemCenter,
        isSystemCenterRef: !!State.systemCenter && State.trajectoryRef === State.systemCenter,
    };
}

// ---------------------------------------------------------------------------
// Worker management
// ---------------------------------------------------------------------------

let worker = null;
let workerBusy = false;
let workerFailed = false; // true if worker creation or communication failed
let pendingResult = null;
let workerStartTime = 0;
let lastPredictionTime = 0;
const PREDICT_FPS_CAP = 12;
const MIN_PREDICT_INTERVAL_MS = 1000 / PREDICT_FPS_CAP;

// Create the worker lazily. Returns null if workers aren't available or
// failed to initialize.
function getWorker() {
    if (workerFailed) return null;
    if (worker) return worker;
    try {
        if (typeof Worker !== "undefined") {
            worker = new Worker(new URL("./prediction-worker.js", import.meta.url), { type: "module" });
            worker.onmessage = (ev) => {
                const msg = ev.data;
                if (msg.type === "done") {
                    pendingResult = msg;
                    workerBusy = false;
                } else if (msg.type === "error") {
                    console.error("Prediction worker error:", msg.error);
                    workerBusy = false;
                    workerFailed = true; // fall back to sync
                }
            };
            worker.onerror = (e) => {
                console.error("Prediction worker error:", e);
                workerBusy = false;
                workerFailed = true; // fall back to sync
            };
            return worker;
        }
    } catch (e) {
        console.warn("Web Workers unavailable, falling back to main-thread prediction:", e);
        workerFailed = true;
    }
    return null;
}

// ---------------------------------------------------------------------------
// Main-thread fallback (synchronous)
// ---------------------------------------------------------------------------

import {
    buildQuadtree, updateEntities, fullClear, setupShip,
} from "./engine.js";

// The fallback runs the entire prediction synchronously when workers are
// unavailable. This causes a single-frame stutter (same as the old behavior),
// but only in environments without Web Worker support. The result is stored
// and returned by pollPrediction().
let fallbackPendingResult = null;

function runFallbackPredictionSync(serialized) {
    // Save real state
    const saved = {
        updateGroup: State.updateGroup,
        quadtree: State.quadtree,
        simCleanupBuffer: State.simCleanupBuffer,
        ghostTrajectories: State.ghostTrajectories,
        ghostTrajectoryColors: State.ghostTrajectoryColors,
        nextID: State.nextID,
        globalTime: State.globalTime,
        delta: State.delta,
        predictDelta: State.predictDelta,
        predictSteps: State.predictSteps,
        simulating: State.simulating,
        authority: State.authority,
        controls: State.controls,
        ownEntity: State.ownEntity,
        trajectoryRef: State.trajectoryRef,
        systemCenter: State.systemCenter,
    };

    // Set up temp state
    State.updateGroup = [];
    State.quadtree = [];
    State.simCleanupBuffer = [];
    State.nextID = serialized.nextID;
    State.globalTime = serialized.globalTime;
    State.delta = serialized.predictDelta;
    State.simulating = true;
    State.authority = true;
    State.controls = { ...serialized.controls };

    const entityMap = new Map();
    for (const s of serialized.entities) {
        let e;
        switch (s.entityType) {
            case Entities.Triangle: e = new Triangle(); break;
            case Entities.CelestialBody: e = new CelestialBody(s.radius, s.mass); break;
            case Entities.Projectile: e = new Projectile(); break;
            case Entities.Missile: e = new Missile(); break;
            default: continue;
        }
        Object.assign(e, s);
        e.id = s.id;
        entityMap.set(e.id, e);
    }
    const allEntities = Array.from(entityMap.values());
    State.ownEntity = serialized.ownEntityId != null ? entityMap.get(serialized.ownEntityId) : null;
    State.systemCenter = serialized.hasSystemCenter ? new CelestialBody(true) : null;

    if (serialized.isSystemCenterRef) {
        State.trajectoryRef = State.systemCenter;
    } else {
        State.trajectoryRef = serialized.trajectoryRefId != null ? entityMap.get(serialized.trajectoryRefId) : null;
    }

    // Re-link targets/owners
    for (const s of serialized.entities) {
        const e = entityMap.get(s.id);
        if (!e) continue;
        if (s.targetId != null) e.target = entityMap.get(s.targetId);
        if (s.ownerId != null) e.owner = entityMap.get(s.ownerId);
    }

    // Ghost ship
    const controlsActive = !!(State.controls.forward || State.controls.backward ||
        State.controls.turnleft || State.controls.turnright || State.controls.boost ||
        State.controls.primaryfire || State.controls.secondaryfire || State.controls.slowrotate);
    if (State.ownEntity && controlsActive) {
        const ghost = new Triangle();
        const ghostId = ghost.id;
        Object.assign(ghost, State.ownEntity);
        ghost.id = ghostId;
        ghost.ghost = true;
        ghost.parent_id = State.ownEntity.id;
        State.simCleanupBuffer.push(ghost);
    }

    for (const e of allEntities) {
        e.simSetup();
        e.trajectory = [];
    }
    if (State.systemCenter) {
        State.systemCenter.trajectory = [];
    }

    // Run prediction loop
    for (let i = 0; i < State.predictSteps; i++) {
        State.globalTime += State.predictDelta;
        buildQuadtree();
        updateEntities();

        if (State.updateGroup.length > 0 && State.trajectoryRef) {
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

        // Record global trajectories
        for (const e of State.updateGroup) {
            e.trajectory.push({ x: e.x, y: e.y });
        }
        if (State.systemCenter) {
            State.systemCenter.trajectory.push({ x: State.systemCenter.x, y: State.systemCenter.y });
        }

        if (State.ownEntity) State.ownEntity.control(State.controls);
        for (let j = 0; j < State.updateGroup.length; j++) {
            if (!State.updateGroup[j].active) {
                State.updateGroup[j].active = true;
                State.updateGroup.splice(j, 1);
                j--;
            }
        }
    }

    // Collect results
    const trajectories = {};
    for (const e of allEntities) {
        if (e.trajectory && e.trajectory.length > 0) {
            trajectories[e.id] = e.trajectory;
        }
    }

    const ghostTraj = [];
    const ghostColors = [];
    for (const en of State.simCleanupBuffer) {
        ghostTraj.push(en.trajectory);
        ghostColors.push([en.color[0] * 0.7, en.color[1] * 0.7, en.color[2] * 0.7]);
    }

    // Restore real state
    State.updateGroup = saved.updateGroup;
    State.quadtree = saved.quadtree;
    State.simCleanupBuffer = saved.simCleanupBuffer;
    State.ghostTrajectories = saved.ghostTrajectories;
    State.ghostTrajectoryColors = saved.ghostTrajectoryColors;
    State.nextID = saved.nextID;
    State.globalTime = saved.globalTime;
    State.delta = saved.delta;
    State.simulating = saved.simulating;
    State.authority = saved.authority;
    State.controls = saved.controls;
    State.ownEntity = saved.ownEntity;
    State.trajectoryRef = saved.trajectoryRef;
    State.systemCenter = saved.systemCenter;

    return { 
        trajectories, 
        systemCenterTrajectory: State.systemCenter ? State.systemCenter.trajectory : null, 
        ghostTrajectories: ghostTraj, 
        ghostTrajectoryColors: ghostColors 
    };
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

export function resetPredictionRateLimit() {
    lastPredictionTime = 0;
}

// Start a prediction. If a worker is available, send state to it. Otherwise,
// run the fallback synchronously and store the result. Returns true if
// prediction was started, false if one is already running.
export function startPrediction() {
    if (workerBusy || fallbackPendingResult) return false;
    if (!State.trajectoryRef) return false;

    const now = performance.now();
    if (now - lastPredictionTime < MIN_PREDICT_INTERVAL_MS) return false;
    lastPredictionTime = now;

    const serialized = serializeState();
    const w = getWorker();

    if (w) {
        workerBusy = true;
        pendingResult = null;
        workerStartTime = now;
        w.postMessage({ type: "predict", state: serialized });
        return true;
    } else {
        // Fallback: synchronous prediction. Result stored for pollPrediction.
        fallbackPendingResult = runFallbackPredictionSync(serialized);
        return true;
    }
}

// Check if a prediction result is ready. If so, apply it to entities and
// return true.
export function pollPrediction(_stepsPerFrame) {
    // Worker path: check for result
    if (pendingResult) {
        const result = pendingResult;
        pendingResult = null;
        applyResult(result);
        return true;
    }

    // Fallback path: result was computed synchronously in startPrediction
    if (fallbackPendingResult) {
        const result = fallbackPendingResult;
        fallbackPendingResult = null;
        applyResult(result);
        return true;
    }

    return false;
}

// Check if prediction is currently running (worker busy or fallback pending).
export function isPredictionRunning() {
    return workerBusy || fallbackPendingResult !== null;
}

// Apply a prediction result to the real game entities.
export function applyResult(result) {
    for (const e of State.updateGroup) {
        e.trajectory = result.trajectories[e.id] || [];
    }
    if (State.systemCenter && result.systemCenterTrajectory) {
        State.systemCenter.trajectory = result.systemCenterTrajectory;
    }
    State.ghostTrajectories = result.ghostTrajectories || [];
    State.ghostTrajectoryColors = result.ghostTrajectoryColors || [];
    State.lastPredict = State.globalTime;
}
