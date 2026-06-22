// Test suite for the rolling trajectory prediction cache.
//
// Run with: node --test client/test/prediction.test.js
//
// Tests use real system generation (generateSystem with default params)
// to exercise realistic scenarios with stars, planets, moons, and the
// player ship.

import { test, describe, beforeEach } from "node:test";
import assert from "node:assert/strict";
import {
    State, CelestialBody, Triangle, generateSystem, fullClear,
    setupShip, updateEntities, buildQuadtree, g_camera,
} from "../engine.js";
import { runPrediction, _internal } from "../prediction.js";

const {
    popPastNodes, checkBodyAgainstCache,
    computePosTolerance, computeVelTolerance,
    interpolateFromCache,
} = _internal;

// Reset all global state and generate a fresh system with default params.
// Returns the player ship.
function setupRealSystem() {
    fullClear(true);
    State.updateGroup = [];
    State.quadtree = [];
    State.simCleanupBuffer = [];
    State.ghostTrajectories = [];
    State.ghostTrajectoryColors = [];
    State.ownEntity = null;
    State.trajectoryRef = null;
    State.lastTrajectoryRef = null;
    State.nextID = 0;
    State.systemCenter = new CelestialBody(true);
    State.globalTime = 0.0;
    State.delta = 1.0 / 60.0;
    State.predictSpacing = 0.25;
    State.predictDelta = 0.4;
    State.predictSteps = Math.floor(90.0 / 0.4); // 225
    State.simulating = false;
    State.authority = true;
    State.controls = {
        forward: 0, backward: 0, turnright: 0, turnleft: 0,
        boost: 0, slowrotate: 0, primaryfire: 0, secondaryfire: 0,
    };
    g_camera.w = 800;
    g_camera.h = 800;
    g_camera.scale = 1.0;

    generateSystem();

    // Create player ship near a random planet.
    const ship = new Triangle();
    ship.name = "TestPlayer";
    ship.setColor(200, 100, 100);
    State.ownEntity = ship;
    setupShip(ship);

    // Auto-select system center as trajectory reference (like the real client).
    State.trajectoryRef = State.systemCenter;
    return ship;
}

// Get all celestial bodies in the current system.
function getBodies() {
    return State.updateGroup.filter(e => e instanceof CelestialBody);
}

// Advance the real simulation by `seconds` using small timesteps.
function advanceRealTime(seconds, dt = 1.0 / 60.0) {
    const steps = Math.floor(seconds / dt);
    const oldDelta = State.delta;
    State.delta = dt;
    for (let i = 0; i < steps; i++) {
        State.globalTime += dt;
        buildQuadtree();
        updateEntities();
    }
    State.delta = oldDelta;
}

// ===========================================================================
// Unit tests for helper functions
// ====================================================================================

describe("popPastNodes", () => {
    test("removes nodes strictly before `now`", () => {
        const cache = {
            nodes: [
                { x: 0, y: 0, velX: 0, velY: 0, t: 1.0 },
                { x: 1, y: 0, velX: 1, velY: 0, t: 1.4 },
                { x: 2, y: 0, velX: 1, velY: 0, t: 1.8 },
            ],
        };
        const removed = popPastNodes(cache, 1.5);
        // 1.0 < 1.5 → pop; 1.4 < 1.5 → pop; 1.8 > 1.5 → keep.
        assert.equal(removed, 2);
        assert.equal(cache.nodes.length, 1);
        assert.equal(cache.nodes[0].t, 1.8);
    });

    test("keeps nodes at exactly `now`", () => {
        const cache = {
            nodes: [
                { x: 0, y: 0, velX: 0, velY: 0, t: 1.0 },
                { x: 1, y: 0, velX: 1, velY: 0, t: 1.4 },
            ],
        };
        const removed = popPastNodes(cache, 1.0);
        assert.equal(removed, 0);
        assert.equal(cache.nodes[0].t, 1.0);
    });

    test("pops nothing when all nodes are in the future", () => {
        const cache = {
            nodes: [
                { x: 0, y: 0, velX: 0, velY: 0, t: 5.0 },
                { x: 1, y: 0, velX: 1, velY: 0, t: 5.4 },
            ],
        };
        const removed = popPastNodes(cache, 1.0);
        assert.equal(removed, 0);
    });

    test("pops everything when all nodes are in the past", () => {
        const cache = {
            nodes: [
                { x: 0, y: 0, velX: 0, velY: 0, t: 1.0 },
                { x: 1, y: 0, velX: 1, velY: 0, t: 1.4 },
            ],
        };
        const removed = popPastNodes(cache, 10.0);
        assert.equal(removed, 2);
        assert.equal(cache.nodes.length, 0);
    });
});

describe("computePosTolerance", () => {
    test("returns minimum tolerance when body is at player position", () => {
        const body = { x: 0, y: 0, radius: 100 };
        const own = { x: 0, y: 0 };
        const tol = computePosTolerance(body, own, 800, 800);
        // distance=0 → scaleMin=0 → tol = max(0, 100*0.05, 2) = 5
        assert.ok(tol >= 2, `tolerance should be at least minimum, got ${tol}`);
    });

    test("scales with distance from player", () => {
        const near = { x: 1000, y: 0, radius: 100 };
        const far = { x: 100000, y: 0, radius: 100 };
        const own = { x: 0, y: 0 };
        const tolNear = computePosTolerance(near, own, 800, 800);
        const tolFar = computePosTolerance(far, own, 800, 800);
        assert.ok(tolFar > tolNear,
            `far body tolerance (${tolFar}) should be larger than near (${tolNear})`);
    });

    test("scales with body radius", () => {
        const small = { x: 50000, y: 0, radius: 100 };
        const big = { x: 50000, y: 0, radius: 45000 };
        const own = { x: 0, y: 0 };
        const tolSmall = computePosTolerance(small, own, 800, 800);
        const tolBig = computePosTolerance(big, own, 800, 800);
        assert.ok(tolBig > tolSmall,
            `big body tolerance (${tolBig}) should be larger than small (${tolSmall})`);
    });

    test("2px at tightest visible zoom", () => {
        // Body at distance d, viewport 800px.
        // Tightest visible zoom: scale_min = 2*d/800 = d/400 m/px.
        // 2px = 2 * d/400 = d/200 meters.
        const d = 60000;
        const body = { x: d, y: 0, radius: 1 }; // tiny radius to isolate distance effect
        const own = { x: 0, y: 0 };
        const tol = computePosTolerance(body, own, 800, 800);
        const expected2px = 2 * (2 * d / 800);
        // tol should be at least expected2px (might be higher due to radius floor).
        assert.ok(tol >= expected2px * 0.99,
            `tolerance ${tol} should be >= 2px value ${expected2px}`);
    });
});

describe("interpolateFromCache", () => {
    test("interpolates linearly between two nodes", () => {
        const body = { x: 0, y: 0, velX: 0, velY: 0 };
        const cache = {
            nodes: [
                { x: 0, y: 0, velX: 10, velY: 0, t: 0 },
                { x: 100, y: 0, velX: 20, velY: 0, t: 1.0 },
            ],
        };
        interpolateFromCache(body, cache, 0.5);
        assert.equal(body.x, 50);
        assert.equal(body.velX, 15);
    });

    test("uses first node when target is at or before it", () => {
        const body = { x: 999, y: 999, velX: 0, velY: 0 };
        const cache = {
            nodes: [
                { x: 10, y: 20, velX: 5, velY: 0, t: 1.0 },
                { x: 20, y: 20, velX: 5, velY: 0, t: 2.0 },
            ],
        };
        interpolateFromCache(body, cache, 0.5);
        assert.equal(body.x, 10);
        assert.equal(body.y, 20);
    });

    test("returns false when target is beyond last node", () => {
        const body = { x: 0, y: 0, velX: 0, velY: 0 };
        const cache = {
            nodes: [
                { x: 0, y: 0, velX: 0, velY: 0, t: 0 },
                { x: 100, y: 0, velX: 0, velY: 0, t: 1.0 },
            ],
        };
        const ok = interpolateFromCache(body, cache, 1.5);
        assert.equal(ok, false);
    });
});

describe("checkBodyAgainstCache", () => {
    test("returns false for empty cache", () => {
        const body = { x: 0, y: 0, velX: 0, velY: 0 };
        assert.equal(checkBodyAgainstCache(body, { nodes: [], stepDt: 0.4 }, 0, 0.4, 10, 10), false);
    });

    test("returns false for single-node cache", () => {
        const body = { x: 0, y: 0, velX: 0, velY: 0 };
        const cache = { nodes: [{ x: 0, y: 0, velX: 0, velY: 0, t: 0 }], stepDt: 0.4 };
        assert.equal(checkBodyAgainstCache(body, cache, 0, 0.4, 10, 10), false);
    });

    test("returns true on direct match", () => {
        const body = { x: 100, y: 200, velX: 10, velY: 20 };
        const cache = {
            nodes: [
                { x: 100, y: 200, velX: 10, velY: 20, t: 0 },
                { x: 110, y: 220, velX: 10, velY: 20, t: 0.4 },
            ],
            stepDt: 0.4,
        };
        assert.equal(checkBodyAgainstCache(body, cache, 0, 0.4, 5, 5), true);
    });

    test("returns false when position is off beyond tolerance", () => {
        const body = { x: 200, y: 200, velX: 10, velY: 20 };
        const cache = {
            nodes: [
                { x: 100, y: 200, velX: 10, velY: 20, t: 0 },
                { x: 110, y: 220, velX: 10, velY: 20, t: 0.4 },
            ],
            stepDt: 0.4,
        };
        assert.equal(checkBodyAgainstCache(body, cache, 0, 0.4, 5, 5), false);
    });

    test("returns false when velocity is off beyond tolerance", () => {
        const body = { x: 100, y: 200, velX: 999, velY: 20 };
        const cache = {
            nodes: [
                { x: 100, y: 200, velX: 10, velY: 20, t: 0 },
                { x: 110, y: 220, velX: 10, velY: 20, t: 0.4 },
            ],
            stepDt: 0.4,
        };
        assert.equal(checkBodyAgainstCache(body, cache, 0, 0.4, 5, 5), false);
    });
});

// ===========================================================================
// Integration tests with real system generation
// ====================================================================================

describe("runPrediction — real system, cold run", () => {
    beforeEach(() => setupRealSystem());

    test("generates a system with multiple celestial bodies", () => {
        const bodies = getBodies();
        assert.ok(bodies.length >= 5, `expected at least 5 bodies, got ${bodies.length}`);
        // Should have at least one star.
        const stars = bodies.filter(b => b.star);
        assert.ok(stars.length >= 1, "should have at least one star");
    });

    test("cold run simulates all bodies from scratch", () => {
        const stats = runPrediction();
        const bodies = getBodies();

        assert.equal(stats.cachedBodies, 0, "no bodies cached on first run");
        // Some bodies may collide during prediction, so newSteps can be
        // less than bodies.length * predictSteps. But it should be close.
        const maxExpected = bodies.length * State.predictSteps;
        assert.ok(stats.newSteps <= maxExpected,
            `new steps (${stats.newSteps}) should be <= ${maxExpected}`);
        assert.ok(stats.newSteps > maxExpected * 0.5,
            `new steps (${stats.newSteps}) should be > 50% of ${maxExpected}`);
        assert.equal(stats.replayedSteps, 0, "nothing replayed on cold run");
    });

    test("all surviving celestial bodies get caches after cold run", () => {
        runPrediction();
        // Bodies that survive the prediction should have a cache. Bodies
        // destroyed mid-prediction (collisions) may have partial or no
        // caches — that's expected. Only check bodies that are still in
        // updateGroup AND have a predCache (i.e. they were alive for at
        // least part of the prediction).
        const survivors = getBodies().filter(b => b.predCache);
        assert.ok(survivors.length > 0, "at least some bodies should have caches");
        for (const b of survivors) {
            assert.ok(b.predCache.nodes.length >= 2,
                `body ${b.id}: cache should have >= 2 nodes, got ${b.predCache.nodes.length}`);
        }
    });

    test("all entities get trajectory arrays", () => {
        runPrediction();
        for (const e of State.updateGroup) {
            assert.ok(e.trajectory.length > 0,
                `entity ${e.id}: should have trajectory points`);
            assert.ok(e.trajectory.length <= State.predictSteps,
                `entity ${e.id}: trajectory too long (${e.trajectory.length} > ${State.predictSteps})`);
        }
    });

    test("globalTime is restored after prediction", () => {
        const t0 = State.globalTime;
        runPrediction();
        assert.equal(State.globalTime, t0);
    });

    test("simulating flag is restored", () => {
        runPrediction();
        assert.equal(State.simulating, false);
    });
});

describe("runPrediction — real system, warm cache", () => {
    beforeEach(() => setupRealSystem());

    test("warm run with no time advance replays most bodies", () => {
        runPrediction(); // cold
        const stats = runPrediction(); // warm

        const bodies = getBodies();
        // Allow some bodies to fail the cache check (collisions during cold
        // run changed the system, float drift in fast moons, etc.).
        const uncached = bodies.length - stats.cachedBodies;
        assert.ok(uncached <= bodies.length * 0.2,
            `at most 20% of bodies should miss cache, got ${uncached}/${bodies.length}`);
        assert.ok(stats.newSteps < bodies.length * State.predictSteps * 0.2,
            `new steps (${stats.newSteps}) should be < 20% of full`);
    });

    test("warm run produces valid trajectories", () => {
        runPrediction(); // cold
        runPrediction(); // warm
        // Trajectories should have at most predictSteps points. Bodies that
        // were destroyed mid-prediction may have fewer.
        for (const e of State.updateGroup) {
            assert.ok(e.trajectory.length > 0,
                `entity ${e.id}: should have trajectory points`);
            assert.ok(e.trajectory.length <= State.predictSteps,
                `entity ${e.id}: trajectory too long (${e.trajectory.length} > ${State.predictSteps})`);
        }
    });

    test("cache stays valid across 10 runs with real time advance", () => {
        runPrediction(); // cold
        for (let i = 0; i < 10; i++) {
            advanceRealTime(State.predictSpacing);
            const stats = runPrediction();
            const bodies = getBodies();
            // Most or all bodies should remain cached. Some might invalidate
            // if they collide or drift, but the majority should be stable.
            const cachedFraction = stats.cachedBodies / bodies.length;
            assert.ok(cachedFraction > 0.3,
                `run ${i}: only ${stats.cachedBodies}/${bodies.length} bodies cached (${(cachedFraction*100).toFixed(0)}%)`);
            assert.ok(stats.newSteps < bodies.length * State.predictSteps,
                `run ${i}: should not be re-simulating everything (${stats.newSteps} new steps)`);
        }
    });
});

describe("runPerformance — real system, rolling prediction", () => {
    beforeEach(() => setupRealSystem());

    test("warm cache does far less simulation work than cold", () => {
        const bodies = getBodies();
        const coldStats = runPrediction();
        // Some bodies might collide during prediction, so cold new steps
        // may be slightly less than bodies.length * predictSteps.
        const coldNewNodes = coldStats.newSteps;
        assert.ok(coldNewNodes > 0, "cold run should produce new nodes");

        const warmStats = runPrediction();
        const warmNewNodes = warmStats.newSteps;

        // Warm run should produce 0 or very few new nodes.
        assert.ok(warmNewNodes < coldNewNodes * 0.1,
            `warm (${warmNewNodes}) should be < 10% of cold (${coldNewNodes})`);
    });

    test("rolling run after 1 predictDelta advance needs few new nodes", () => {
        const bodies = getBodies();
        runPrediction(); // cold
        advanceRealTime(State.predictDelta); // advance ~1 step

        const stats = runPrediction();
        const fullWork = bodies.length * State.predictSteps;
        // After 1 step advance, we need ~1 new node per cached body.
        // Some bodies may invalidate (collisions, drift) and need full re-sim.
        assert.ok(stats.newSteps < fullWork * 0.5,
            `new steps (${stats.newSteps}) should be < 50% of full (${fullWork})`);
        assert.ok(stats.cachedBodies > 0, "should have cached bodies");
    });

    test("rolling run after 5s advance still benefits from cache", () => {
        const bodies = getBodies();
        runPrediction(); // cold
        advanceRealTime(5.0); // advance 5 seconds

        const stats = runPrediction();
        const fullWork = bodies.length * State.predictSteps;
        // After 5s (~12 predictDeltas), cached bodies need ~12 new nodes.
        // Some bodies may invalidate entirely. Overall should still be
        // much less than full re-simulation.
        assert.ok(stats.newSteps < fullWork * 0.5,
            `new steps (${stats.newSteps}) should be < 50% of full (${fullWork})`);
    });
});

describe("runPrediction — cache invalidation", () => {
    beforeEach(() => setupRealSystem());

    test("invalidates cache when body position is externally changed", () => {
        runPrediction();
        const bodies = getBodies();
        const planet = bodies.find(b => !b.star) || bodies[0];

        // Teleport the planet (simulates a server sync correction).
        planet.x += 100000;
        planet.y += 100000;

        const stats = runPrediction();
        // At least the teleported planet should be invalidated.
        assert.ok(stats.simulatedBodies >= 1,
            `at least 1 body should be re-simulated, got ${stats.simulatedBodies}`);
    });

    test("invalidates all caches when reference body changes", () => {
        runPrediction();
        const bodies = getBodies();
        // Change reference to a specific body.
        State.trajectoryRef = bodies[0];

        const stats = runPrediction();
        assert.equal(stats.cachedBodies, 0, "no bodies cached after ref change");
        assert.equal(stats.simulatedBodies, bodies.length, "all bodies re-simulated");
    });
});

describe("runPrediction — ghost ship", () => {
    beforeEach(() => setupRealSystem());

    test("ghost ship is created when controls are active", () => {
        State.controls.forward = 1;
        runPrediction();
        assert.ok(State.ghostTrajectories.length > 0, "ghost should exist");
        assert.equal(State.ghostTrajectories[0].length, State.predictSteps);
    });

    test("no ghost ship when controls are inactive", () => {
        runPrediction();
        assert.equal(State.ghostTrajectories.length, 0);
    });
});

describe("runPrediction — correctness", () => {
    beforeEach(() => setupRealSystem());

    test("warm cache trajectory is close to cold cache trajectory for cached bodies", () => {
        // Cold run.
        runPrediction();
        const coldTraj = State.updateGroup.map(e => ({
            id: e.id,
            traj: e.trajectory.map(p => ({ x: p.x, y: p.y })),
            posTol: e instanceof CelestialBody
                ? computePosTolerance(e, State.ownEntity, g_camera.w, g_camera.h)
                : 0,
        }));

        // Warm run (no time advance — should replay same positions).
        runPrediction();

        // For cached bodies that were replayed, compare the first 20
        // trajectory nodes. We only compare the early portion because
        // later nodes may diverge if the system had collisions during the
        // cold run (changing gravity for other bodies). The early nodes
        // should be very close since the cache was just built.
        let compared = 0;
        for (let i = 0; i < State.updateGroup.length; i++) {
            const e = State.updateGroup[i];
            if (!(e instanceof CelestialBody)) continue;
            if (!e.predCache || e.predCache.nodes.length < 2) continue;

            const cold = coldTraj[i].traj;
            const tol = Math.max(100, coldTraj[i].posTol * 5);
            const compareLen = Math.min(20, cold.length, e.trajectory.length);
            for (let j = 0; j < compareLen; j++) {
                const dx = Math.abs(e.trajectory[j].x - cold[j].x);
                const dy = Math.abs(e.trajectory[j].y - cold[j].y);
                assert.ok(dx < tol,
                    `body ${e.id} node ${j}: dx=${dx} > tol=${tol}`);
                assert.ok(dy < tol,
                    `body ${e.id} node ${j}: dy=${dy} > tol=${tol}`);
            }
            compared++;
        }
        assert.ok(compared > 0, "should have compared at least one body");
    });
});
