// Trajectory prediction coordinator.
//
// Manages prediction in a Web Worker to avoid stutter on the main thread.
// The worker runs the full prediction loop (via prediction-core.js) and
// posts back trajectories in world coordinates. The main thread applies
// them to entities for rendering.
//
// If Web Workers are unavailable, falls back to synchronous main-thread
// prediction (also via prediction-core.js, wrapped with state save/restore).
//
// Trajectories are stored in WORLD coordinates — the ref-body transform is
// applied only at draw time (in drawTrajectory). This means trajectories
// don't need to be recomputed when the ref-body changes.

import { State, Triangle, CelestialBody, Projectile, Missile, g_camera } from "./engine.js";
import { Entities } from "./types.js";
import { runPredictionCore } from "./prediction-core.js";

// ---------------------------------------------------------------------------
// State serialization
// ---------------------------------------------------------------------------

// Serialize the current game state for transfer to the worker.
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
        if (e instanceof Triangle) {
            s.name = e.name;
            s.boostProgress = e.boostProgress;
            s.reloadProgress = e.reloadProgress;
            s.secondaryCharge = e.secondaryCharge;
            s.secondaryProgress = e.secondaryProgress;
            s.targetId = e.target ? e.target.id : null;
        }
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
        maneuverNodes: State.maneuverScheduler ? State.maneuverScheduler.serialize().nodes : [],
    };
}

// ---------------------------------------------------------------------------
// Worker management
// ---------------------------------------------------------------------------

let worker = null;
let workerBusy = false;
let workerFailed = false;
let pendingResult = null;
let workerStartTime = 0;
let lastPredictionTime = 0;
const PREDICT_FPS_CAP = 12;
const MIN_PREDICT_INTERVAL_MS = 1000 / PREDICT_FPS_CAP;

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
                    workerFailed = true;
                }
            };
            worker.onerror = (e) => {
                console.error("Prediction worker error:", e);
                workerBusy = false;
                workerFailed = true;
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

// The fallback saves the real game state, runs prediction-core.js (which
// overwrites State), then restores the real state. The result is stored
// and returned by pollPrediction().
let fallbackPendingResult = null;

function runFallbackPredictionSync(serialized) {
    // Save all State fields that prediction-core modifies
    const saved = {
        updateGroup: State.updateGroup,
        quadtree: State.quadtree,
        simCleanupBuffer: State.simCleanupBuffer,
        ghostTrajectories: State.ghostTrajectories,
        ghostTrajectoryStarts: State.ghostTrajectoryStarts,
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

    const result = runPredictionCore(serialized);

    // Restore real state
    State.updateGroup = saved.updateGroup;
    State.quadtree = saved.quadtree;
    State.simCleanupBuffer = saved.simCleanupBuffer;
    State.ghostTrajectories = saved.ghostTrajectories;
    State.ghostTrajectoryStarts = saved.ghostTrajectoryStarts;
    State.ghostTrajectoryColors = saved.ghostTrajectoryColors;
    State.nextID = saved.nextID;
    State.globalTime = saved.globalTime;
    State.delta = saved.delta;
    State.predictDelta = saved.predictDelta;
    State.predictSteps = saved.predictSteps;
    State.simulating = saved.simulating;
    State.authority = saved.authority;
    State.controls = saved.controls;
    State.ownEntity = saved.ownEntity;
    State.trajectoryRef = saved.trajectoryRef;
    State.systemCenter = saved.systemCenter;

    return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

export function resetPredictionRateLimit() {
    lastPredictionTime = 0;
}

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
        fallbackPendingResult = runFallbackPredictionSync(serialized);
        return true;
    }
}

export function pollPrediction(_stepsPerFrame) {
    if (pendingResult) {
        const result = pendingResult;
        pendingResult = null;
        applyResult(result);
        return true;
    }
    if (fallbackPendingResult) {
        const result = fallbackPendingResult;
        fallbackPendingResult = null;
        applyResult(result);
        return true;
    }
    return false;
}

export function isPredictionRunning() {
    return workerBusy || fallbackPendingResult !== null;
}

export function applyResult(result) {
    for (const e of State.updateGroup) {
        e.trajectory = result.trajectories[e.id] || [];
        e.trajectoryStartTime = result.trajectoryStarts[e.id] || [];
    }
    if (State.systemCenter && result.systemCenterTrajectory) {
        State.systemCenter.trajectory = result.systemCenterTrajectory;
    }
    State.ghostTrajectories = result.ghostTrajectories || [];
    State.ghostTrajectoryStarts = result.ghostTrajectoryStarts || [];
    State.ghostTrajectoryColors = result.ghostTrajectoryColors || [];
    State.lastPredict = State.globalTime;
}
