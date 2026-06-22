// Web Worker for trajectory prediction.
//
// Runs in a separate thread to avoid stutter on the main thread. Receives
// a serialized game state, runs the prediction loop, and posts back
// trajectories in world coordinates.
//
// The worker imports engine.js directly — it has no browser dependencies,
// so it works in both Web Workers and Node.js worker_threads.
//
// Message protocol:
//   Main → Worker: { type: "predict", state, config }
//   Worker → Main: { type: "done", trajectories, ghostTrajectories, ... }

import {
    State, Triangle, CelestialBody, Projectile, Missile,
    generateSystem, fullClear, setupShip,
    buildQuadtree, updateEntities,
} from "./engine.js";
import { Entities } from "./types.js";

// ---------------------------------------------------------------------------
// State deserialization
// ---------------------------------------------------------------------------

// Rebuild entities from serialized data. Each entity is a plain object
// with a `type` field indicating its class. We create new instances and
// copy over the serialized fields.
function deserializeState(data) {
    fullClear(true);
    State.updateGroup = [];
    State.quadtree = [];
    State.simCleanupBuffer = [];
    State.ghostTrajectories = [];
    State.ghostTrajectoryColors = [];
    State.nextID = data.nextID;
    State.globalTime = data.globalTime;
    State.delta = data.predictDelta;
    State.predictDelta = data.predictDelta;
    State.predictSteps = data.predictSteps;
    State.simulating = false;
    State.authority = true;
    State.controls = { ...data.controls };

    const entityMap = new Map(); // id → entity

    for (const s of data.entities) {
        let e;
        switch (s.entityType) {
            case Entities.Triangle:
                e = new Triangle();
                break;
            case Entities.CelestialBody:
                e = new CelestialBody(s.radius, s.mass);
                break;
            case Entities.Projectile:
                e = new Projectile();
                break;
            case Entities.Missile:
                e = new Missile();
                break;
            default:
                continue;
        }
        // Copy serialized fields (but preserve id assigned by constructor)
        const newId = e.id;
        Object.assign(e, s);
        e.id = newId; // keep the original id from serialized data
        // Wait — we want the entity to have the SAME id as in the main thread
        // so trajectories can be matched back. Let me use the serialized id.
        e.id = s.id;
        entityMap.set(e.id, e);
    }

    // Set ownEntity
    if (data.ownEntityId != null) {
        State.ownEntity = entityMap.get(data.ownEntityId);
    }

    // Re-link targets/owners for missiles
    for (const s of data.entities) {
        if (s.targetId != null) {
            const e = entityMap.get(s.id);
            if (e) e.target = entityMap.get(s.targetId);
        }
        if (s.ownerId != null) {
            const e = entityMap.get(s.id);
            if (e) e.owner = entityMap.get(s.ownerId);
        }
    }

    State.systemCenter = data.hasSystemCenter ? new CelestialBody(true) : null;

    if (data.isSystemCenterRef) {
        State.trajectoryRef = State.systemCenter;
    } else {
        State.trajectoryRef = data.trajectoryRefId != null
            ? entityMap.get(data.trajectoryRefId)
            : null;
    }

    return entityMap;
}

// ---------------------------------------------------------------------------
// Prediction loop (runs entirely in the worker)
// ---------------------------------------------------------------------------

function runPredictionInWorker(data) {
    const entityMap = deserializeState(data);
    const allEntities = Array.from(entityMap.values());

    State.delta = State.predictDelta;
    State.simulating = true;
    State.ghostTrajectories = [];
    State.ghostTrajectoryColors = [];

    const controlsActive = !!(State.controls.forward || State.controls.backward ||
        State.controls.turnleft || State.controls.turnright || State.controls.boost ||
        State.controls.primaryfire || State.controls.secondaryfire || State.controls.slowrotate);

    // Create ghost ship if controls are active
    if (State.ownEntity && controlsActive) {
        const ghost = new Triangle();
        const ghostId = ghost.id;
        Object.assign(ghost, State.ownEntity);
        ghost.id = ghostId;
        ghost.ghost = true;
        ghost.parent_id = State.ownEntity.id;
        State.simCleanupBuffer.push(ghost);
    }

    // Initialize trajectory arrays
    for (const e of allEntities) {
        e.simSetup();
        e.trajectory = [];
    }
    if (State.systemCenter) {
        State.systemCenter.trajectory = [];
    }

    // Run the prediction loop
    for (let i = 0; i < State.predictSteps; i++) {
        State.predictingFor = State.predictDelta * State.predictSteps;
        State.globalTime += State.predictDelta;

        buildQuadtree();
        updateEntities();

        // Update system center (for rendering reference)
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

        // Record trajectory positions in global coordinates
        for (const e of State.updateGroup) {
            e.trajectory.push({ x: e.x, y: e.y });
        }
        if (State.systemCenter) {
            State.systemCenter.trajectory.push({ x: State.systemCenter.x, y: State.systemCenter.y });
        }

        if (State.ownEntity) State.ownEntity.control(State.controls);

        // Remove dead entities
        for (let j = 0; j < State.updateGroup.length; j++) {
            if (!State.updateGroup[j].active) {
                State.updateGroup[j].active = true;
                State.updateGroup.splice(j, 1);
                j--;
            }
        }
    }

    // Collect ghost trajectories
    for (const en of State.simCleanupBuffer) {
        State.ghostTrajectories.push(en.trajectory);
        State.ghostTrajectoryColors.push([en.color[0] * 0.7, en.color[1] * 0.7, en.color[2] * 0.7]);
    }

    // Build result: trajectories keyed by entity ID (world coordinates)
    const trajectories = {};
    for (const e of allEntities) {
        if (e.trajectory && e.trajectory.length > 0) {
            trajectories[e.id] = e.trajectory;
        }
    }

    return {
        trajectories,
        systemCenterTrajectory: State.systemCenter ? State.systemCenter.trajectory : null,
        ghostTrajectories: State.ghostTrajectories,
        ghostTrajectoryColors: State.ghostTrajectoryColors,
    };
}

// ---------------------------------------------------------------------------
// Message handler
// ---------------------------------------------------------------------------

let isRunning = false;

self.onmessage = (ev) => {
    const msg = ev.data;
    if (msg.type === "predict" && !isRunning) {
        isRunning = true;
        try {
            const result = runPredictionInWorker(msg.state);
            self.postMessage({ type: "done", ...result });
        } catch (e) {
            self.postMessage({ type: "error", error: e.message || String(e) });
        }
        isRunning = false;
    }
};
