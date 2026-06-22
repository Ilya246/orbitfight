// Trajectory prediction with rolling cache + interpolation.
//
// Extracted from game.js so it can be unit-tested in Node.js without any
// browser dependencies. The prediction loop simulates the whole system
// forward by `predictSteps` steps of `predictDelta` each, recording each
// entity's position relative to the trajectory reference body. The result
// is drawn as dashed orbit lines.
//
// Cache strategy (rolling, interpolation-based):
//
// Celestial bodies move deterministically under gravity and are expensive
// to re-simulate. We cache their predicted world-space trajectory and, on
// subsequent runs, interpolate cached positions instead of simulating.
//
// Each cached node stores { x, y, velX, velY, t } where t is the absolute
// simulation time. Node[0] is the body's state at the time the cache was
// built; node[k] is the state k*stepDt later. On each prediction run:
//
//   1. POP: remove nodes whose time is strictly before `now`.
//   2. VALIDATE: check that the body's current state matches the first
//      cached node (directly if times align, or by extrapolation if not).
//      Tolerance is pixel-based: the position error must be < 2 pixels at
//      the tightest zoom where the body is visible.
//   3. REPLAY: during the prediction loop, interpolate the body's state
//      from the two cached nodes bracketing each step's target time. The
//      body still goes into the quadtree so other entities can attract to
//      it and collide with it.
//   4. EXTEND: once we've exhausted cached nodes, continue the loop with
//      real simulation for the remaining steps. Append each new node.
//
// Ships, projectiles, and missiles are never cached.

import {
    State, Triangle, CelestialBody,
    buildQuadtree, updateEntities, g_camera,
} from "./engine.js";

// ---------------------------------------------------------------------------
// Tolerance computation (pixel-based)
// ---------------------------------------------------------------------------

// Minimum position tolerance (meters). Bodies very close to the player or
// very large need at least this much to absorb float/integration noise.
const POS_TOL_MIN = 2.0;
// Velocity tolerance as a fraction of the body's speed, with a floor.
const VEL_TOL_MIN = 1.0;       // m/s
const VEL_TOL_FRAC = 0.02;     // 2% of speed

// Compute the position tolerance for a body based on how visible a position
// error would be to the player. The idea: if the body is far away, the
// player must be zoomed out to see it, so each pixel covers more meters
// and we can tolerate a larger position error.
//
// Specifically: the tightest zoom at which the body is still on-screen is
// scale_min = 2 * distance / min(viewportW, viewportH). At that zoom, 2
// pixels = 2 * scale_min meters. We use that as the tolerance, with a
// floor based on the body's radius (so the trajectory doesn't visibly
// detach from the body's surface).
function computePosTolerance(body, ownEntity, camW, camH) {
    if (!ownEntity) return POS_TOL_MIN;
    const dx = body.x - ownEntity.x;
    const dy = body.y - ownEntity.y;
    const distance = Math.sqrt(dx * dx + dy * dy);
    const minDim = Math.max(1, Math.min(camW, camH));
    // Tightest zoom (m/pixel) where body is on-screen.
    const scaleMin = (2 * distance) / minDim;
    // 2 pixels at that zoom.
    let tol = 2 * scaleMin;
    // Also at least 5% of body radius (so trajectory doesn't detach
    // from the body's surface visually).
    tol = Math.max(tol, body.radius * 0.05);
    // And a minimum absolute floor.
    tol = Math.max(tol, POS_TOL_MIN);
    return tol;
}

function computeVelTolerance(body) {
    const speed = Math.sqrt(body.velX * body.velX + body.velY * body.velY);
    return Math.max(VEL_TOL_MIN, speed * VEL_TOL_FRAC);
}

// ---------------------------------------------------------------------------
// Cache validity check
// ---------------------------------------------------------------------------

// Check whether a celestial body's current state is consistent with its
// cached trajectory. Returns true if the cache can be replayed.
//
// After popPastNodes, the first remaining node is at t0 >= now (we keep
// nodes at exactly `now`). If |t0 - now| < 1ms, compare directly.
// Otherwise extrapolate backward using a constant-acceleration model.
function checkBodyAgainstCache(body, cache, now, dt, posTol, velTol) {
    if (!cache || !cache.nodes || cache.nodes.length < 2) return false;
    if (cache.stepDt !== dt) return false;

    const n0 = cache.nodes[0];
    const dt0 = now - n0.t;

    if (Math.abs(dt0) < 0.001) {
        // Direct comparison.
        if (Math.abs(body.x - n0.x) > posTol) return false;
        if (Math.abs(body.y - n0.y) > posTol) return false;
        if (Math.abs(body.velX - n0.velX) > velTol) return false;
        if (Math.abs(body.velY - n0.velY) > velTol) return false;
        return true;
    }

    // Extrapolate backward from first two nodes (constant acceleration).
    const n1 = cache.nodes[1];
    const nodeDt = n1.t - n0.t;
    const accelX = (n1.velX - n0.velX) / nodeDt;
    const accelY = (n1.velY - n0.velY) / nodeDt;
    const expectedX = n0.x + n0.velX * dt0 + 0.5 * accelX * dt0 * dt0;
    const expectedY = n0.y + n0.velY * dt0 + 0.5 * accelY * dt0 * dt0;
    const expectedVX = n0.velX + accelX * dt0;
    const expectedVY = n0.velY + accelY * dt0;

    if (Math.abs(body.x - expectedX) > posTol) return false;
    if (Math.abs(body.y - expectedY) > posTol) return false;
    if (Math.abs(body.velX - expectedVX) > velTol) return false;
    if (Math.abs(body.velY - expectedVY) > velTol) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Node popping
// ---------------------------------------------------------------------------

// Remove cached nodes whose time is strictly before `now`.
// We keep nodes at exactly `now` for direct comparison.
function popPastNodes(cache, now) {
    let removed = 0;
    while (cache.nodes.length > 0 && cache.nodes[0].t < now) {
        cache.nodes.shift();
        removed++;
    }
    return removed;
}

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------

// Set a body's state by interpolating between two cached nodes at the
// given target time. Uses linear interpolation for position and velocity
// (constant-acceleration would be slightly better, but linear is fine for
// sub-stepDt intervals).
function interpolateFromCache(body, cache, targetTime) {
    const nodes = cache.nodes;
    if (nodes.length === 0) return false;

    // If target is at or before the first node, use the first node.
    if (targetTime <= nodes[0].t) {
        const n = nodes[0];
        body.x = n.x; body.y = n.y;
        body.velX = n.velX; body.velY = n.velY;
        return true;
    }
    // If target is at or after the last node, we can't interpolate.
    if (targetTime >= nodes[nodes.length - 1].t) return false;

    // Binary search for the bracketing pair.
    let lo = 0, hi = nodes.length - 1;
    while (lo < hi - 1) {
        const mid = (lo + hi) >> 1;
        if (nodes[mid].t <= targetTime) lo = mid;
        else hi = mid;
    }
    const n0 = nodes[lo], n1 = nodes[hi];
    const span = n1.t - n0.t;
    const frac = span > 0 ? (targetTime - n0.t) / span : 0;
    body.x = n0.x + (n1.x - n0.x) * frac;
    body.y = n0.y + (n1.y - n0.y) * frac;
    body.velX = n0.velX + (n1.velX - n0.velX) * frac;
    body.velY = n0.velY + (n1.velY - n0.velY) * frac;
    return true;
}

// ---------------------------------------------------------------------------
// Main prediction function
// ---------------------------------------------------------------------------

export function runPrediction() {
    const resDelta = State.delta;
    const resTime = State.globalTime;
    const resAuthority = State.authority;
    const resControls = { ...State.controls };
    State.authority = true;
    const retUpdateGroup = State.updateGroup.slice();
    State.delta = State.predictDelta;
    State.simulating = true;
    State.ghostTrajectories = [];
    State.ghostTrajectoryColors = [];

    const now = State.globalTime;
    const dt = State.predictDelta;
    const refId = State.trajectoryRef ? State.trajectoryRef.id : -1;
    const camW = g_camera.w || 800;
    const camH = g_camera.h || 800;

    const controlsActive = !!(State.controls.forward || State.controls.backward ||
        State.controls.turnleft || State.controls.turnright || State.controls.boost ||
        State.controls.primaryfire || State.controls.secondaryfire || State.controls.slowrotate);

    let ghost = null;
    if (State.ownEntity && controlsActive) {
        ghost = new Triangle();
        const id = ghost.id;
        Object.assign(ghost, State.ownEntity);
        ghost.id = id;
        ghost.ghost = true;
        ghost.parent_id = State.ownEntity.id;
        State.simCleanupBuffer.push(ghost);
    }

    // Phase 1: Decide which celestial bodies can replay from cache.
    const replayBodies = [];
    const simBodies = [];

    for (const e of State.updateGroup) {
        e.simSetup();
        e.trajectory = [];

        if (e instanceof CelestialBody) {
            if (e.predCache) popPastNodes(e.predCache, now);

            const posTol = computePosTolerance(e, State.ownEntity, camW, camH);
            const velTol = computeVelTolerance(e);

            if (e.predCache && e.predCache.refId === refId &&
                checkBodyAgainstCache(e, e.predCache, now, dt, posTol, velTol)) {
                e.predReplaying = true;
                replayBodies.push(e);
            } else {
                // Rebuild from scratch. Store initial state as node[0].
                e.predCache = {
                    refId, stepDt: dt, baseTime: now,
                    nodes: [{ x: e.x, y: e.y, velX: e.velX, velY: e.velY, t: now }],
                };
                e.predReplaying = false;
                simBodies.push(e);
            }
        } else {
            e.predReplaying = false;
        }
    }

    // Phase 2: Prediction loop.
    //
    // For replay bodies, we interpolate from the cache at each step's
    // target time. If the target time is beyond the last cached node,
    // the body switches to live simulation for the remaining steps.
    let replayedSteps = 0;
    let newSteps = 0;

    for (let i = 0; i < State.predictSteps; i++) {
        State.predictingFor = State.predictDelta * State.predictSteps;
        State.globalTime += State.predictDelta;
        const stepTime = State.globalTime;

        // Set replay bodies' states from cache via interpolation.
        for (const e of replayBodies) {
            if (e.predReplaying) {
                const ok = interpolateFromCache(e, e.predCache, stepTime);
                if (ok) {
                    replayedSteps++;
                } else {
                    // Exhausted cache — switch to live sim.
                    e.predReplaying = false;
                    simBodies.push(e);
                }
            }
        }

        buildQuadtree();
        updateEntities();

        // Record trajectory positions for rendering.
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
            for (const e of State.updateGroup) {
                e.trajectory.push({ x: e.x - State.trajectoryRef.x, y: e.y - State.trajectoryRef.y });
            }
        }

        // Capture new cache nodes for bodies that simulated this step.
        for (const e of simBodies) {
            if (e instanceof CelestialBody && e.active && e.predCache) {
                e.predCache.nodes.push({ x: e.x, y: e.y, velX: e.velX, velY: e.velY, t: stepTime });
                newSteps++;
            }
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

    // Phase 3: Finalize — clear caches for dead bodies.
    for (const e of replayBodies) {
        if (!e.active || !e.predCache || e.predCache.nodes.length === 0) {
            e.predCache = null;
        }
    }
    for (const e of simBodies) {
        if (e instanceof CelestialBody && (!e.active || !e.predCache || e.predCache.nodes.length === 0)) {
            e.predCache = null;
        }
    }

    for (const en of State.simCleanupBuffer) {
        State.ghostTrajectories.push(en.trajectory);
        State.ghostTrajectoryColors.push([en.color[0] * 0.7, en.color[1] * 0.7, en.color[2] * 0.7]);
        en.active = false;
    }
    State.simCleanupBuffer = [];
    State.updateGroup = retUpdateGroup;
    for (const e of State.updateGroup) e.simReset();
    State.delta = resDelta;
    State.simulating = false;
    State.authority = resAuthority;
    State.controls = resControls;
    State.globalTime = resTime;
    State.lastPredict = State.globalTime;
    State.lastTrajectoryRef = State.trajectoryRef;

    return {
        cachedBodies: replayBodies.length,
        simulatedBodies: simBodies.filter(e => e instanceof CelestialBody).length,
        totalSteps: State.predictSteps,
        replayedSteps,
        newSteps,
    };
}

// Exported for testing.
export const _internal = {
    POS_TOL_MIN,
    VEL_TOL_MIN,
    VEL_TOL_FRAC,
    popPastNodes,
    checkBodyAgainstCache,
    computePosTolerance,
    computeVelTolerance,
    interpolateFromCache,
};
