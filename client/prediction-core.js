// Shared prediction core — used by both the Web Worker and the main-thread
// fallback. Takes a serialized game state, runs the full prediction loop,
// and returns trajectories in world coordinates.
//
// This module manipulates the global `State` object directly. The caller
// (worker or fallback) is responsible for saving/restoring real state if
// needed.

import {
    State, Triangle, CelestialBody, Projectile, Missile,
    buildQuadtree, updateEntities,
} from "./engine.js";
import { Entities } from "./types.js";
import {
    spawnManeuverGhosts, stepManeuverGhosts, collectManeuverGhosts,
} from "./maneuver.js";

export function runPredictionCore(data) {
    // ---- Reset state for prediction ----
    State.updateGroup = [];
    State.quadtree = [];
    State.simCleanupBuffer = [];
    State.ghostTrajectories = [];
    State.ghostTrajectoryStarts = [];
    State.ghostTrajectoryColors = [];
    State.ownEntity = null;
    State.trajectoryRef = null;
    State.nextID = data.nextID;
    State.globalTime = data.globalTime;
    State.delta = data.predictDelta;
    State.predictDelta = data.predictDelta;
    State.predictSteps = data.predictSteps;
    State.simulating = true;
    State.authority = true;
    State.controls = { ...data.controls };

    // ---- Deserialize entities ----
    const entityMap = new Map();
    for (const s of data.entities) {
        let e;
        switch (s.entityType) {
            case Entities.Triangle:    e = new Triangle(); break;
            case Entities.CelestialBody: e = new CelestialBody(s.radius, s.mass); break;
            case Entities.Projectile:  e = new Projectile(); break;
            case Entities.Missile:     e = new Missile(); break;
            default: continue;
        }
        Object.assign(e, s);
        e.id = s.id;
        entityMap.set(e.id, e);
    }
    const allEntities = Array.from(entityMap.values());

    // Set ownEntity
    State.ownEntity = data.ownEntityId != null ? entityMap.get(data.ownEntityId) : null;

    // Re-link targets/owners
    for (const s of data.entities) {
        const e = entityMap.get(s.id);
        if (!e) continue;
        if (s.targetId != null) e.target = entityMap.get(s.targetId);
        if (s.ownerId != null) e.owner = entityMap.get(s.ownerId);
    }

    // Set systemCenter and trajectoryRef
    State.systemCenter = data.hasSystemCenter ? new CelestialBody(true) : null;
    if (data.isSystemCenterRef) {
        State.trajectoryRef = State.systemCenter;
    } else {
        State.trajectoryRef = data.trajectoryRefId != null
            ? entityMap.get(data.trajectoryRefId)
            : null;
    }

    // ---- Create ghosts ----

    // Control ghost: shows where the ship would go if current controls
    // are held. Only created if any controls are active.
    const controlsActive = !!(State.controls.forward || State.controls.backward ||
        State.controls.turnleft || State.controls.turnright || State.controls.boost ||
        State.controls.primaryfire || State.controls.secondaryfire || State.controls.slowrotate);
    if (State.ownEntity && controlsActive) {
        const ghost = new Triangle();
        const ghostId = ghost.id;
        Object.assign(ghost, State.ownEntity);
        ghost.id = ghostId;
        ghost.ghost = true;
        ghost.phantom = true;
        ghost.parent_id = State.ownEntity.id;
        ghost.trajectory = [];
        State.simCleanupBuffer.push(ghost);
    }

    // ---- Initialize trajectory arrays ----
    for (const e of allEntities) {
        e.simSetup();
        e.trajectory = [];
        e.trajectoryStartTime = null;
    }
    if (State.systemCenter) {
        State.systemCenter.trajectory = [];
    }

    // Maneuver ghosts: one phantom ghost per node; ghost k executes nodes 0..k.
    spawnManeuverGhosts(data.maneuverNodes || [], State.ownEntity);

    const startGlobalTime = data.globalTime;

    // ---- Run prediction loop ----
    for (let i = 0; i < State.predictSteps; i++) {
        State.predictingFor = State.predictDelta * State.predictSteps;
        State.globalTime += State.predictDelta;

        buildQuadtree();
        updateEntities();

        // Update maneuver ghosts (apply burns) after physics
        stepManeuverGhosts(State.globalTime, State.predictDelta);

        // Update system center (center of mass of all entities)
        if (State.updateGroup.length > 0 && State.trajectoryRef) {
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

        // Record trajectory positions in world coordinates.
        // Maneuver ghosts record from their first burn onward (recording flag).
        for (const e of State.updateGroup) {
            if (e.maneuverNodeId != null && !e.recording) continue;
            if (e.trajectory.length === 0) e.trajectoryStartTime = i;
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

    // ---- Collect maneuver ghost results ----
    const maneuverGhosts = collectManeuverGhosts(startGlobalTime, State.predictDelta);

    // ---- Collect results ----
    const trajectories = {};
    const trajectoryStarts = {};
    for (const e of allEntities) {
        if (e.trajectory && e.trajectory.length > 0) {
            trajectories[e.id] = e.trajectory;
            trajectoryStarts[e.id] = e.trajectoryStartTime;
        }
    }

    const ghostTraj = [];
    const ghostStarts = [];
    const ghostColors = [];
    for (const en of State.simCleanupBuffer) {
        // Skip maneuver ghosts — they're returned separately
        if (en.maneuverNodeId != null) continue;
        ghostTraj.push(en.trajectory);
        ghostStarts.push(en.trajectoryStartTime);
        ghostColors.push([en.color[0] * 0.7, en.color[1] * 0.7, en.color[2] * 0.7]);
    }

    return {
        trajectories,
        trajectoryStarts,
        systemCenterTrajectory: State.systemCenter ? State.systemCenter.trajectory : null,
        ghostTrajectories: ghostTraj,
        ghostTrajectoryStarts: ghostStarts,
        ghostTrajectoryColors: ghostColors,
        maneuverGhosts,
        predStartTime: startGlobalTime,
    };
}
