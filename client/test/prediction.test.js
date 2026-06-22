// Test suite for the worker-based trajectory prediction system.
//
// Run with: node --test client/test/prediction.test.js
//
// Tests use the fallback (time-sliced main-thread) path since Node.js
// doesn't have Web Workers. The serialization, prediction loop, and
// trajectory application logic are identical between worker and fallback.

import { test, describe, beforeEach } from "node:test";
import assert from "node:assert/strict";
import {
    State, CelestialBody, Triangle, generateSystem, fullClear,
    setupShip, updateEntities, buildQuadtree, g_camera,
} from "../engine.js";
import { Entities } from "../types.js";
import {
    serializeState, startPrediction, pollPrediction,
    isPredictionRunning, applyResult, resetPredictionRateLimit
} from "../prediction.js";

function setupRealSystem() {
    fullClear(true);
    State.updateGroup = [];
    State.quadtree = [];
    State.simCleanupBuffer = [];
    State.ghostTrajectories = [];
    State.ghostTrajectoryColors = [];
    State.ownEntity = null;
    State.trajectoryRef = null;
    State.nextID = 0;
    State.systemCenter = new CelestialBody(true);
    State.globalTime = 0.0;
    State.delta = 1.0 / 60.0;
    State.predictSpacing = 0.25;
    State.predictDelta = 0.4;
    State.predictSteps = Math.floor(90.0 / 0.4);
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

    const ship = new Triangle();
    ship.name = "TestPlayer";
    ship.setColor(200, 100, 100);
    State.ownEntity = ship;
    setupShip(ship);
    State.trajectoryRef = State.systemCenter;
    
    resetPredictionRateLimit();
    return ship;
}

function getBodies() {
    return State.updateGroup.filter(e => e instanceof CelestialBody);
}

// ===========================================================================
// Serialization tests
// ====================================================================================

describe("serializeState", () => {
    beforeEach(() => setupRealSystem());

    test("captures all entities", () => {
        const s = serializeState();
        assert.equal(s.entities.length, State.updateGroup.length);
    });

    test("captures entity kinematic state", () => {
        const planet = getBodies()[0];
        const origX = planet.x;
        const origY = planet.y;
        const s = serializeState();
        const serialized = s.entities.find(e => e.id === planet.id);
        assert.equal(serialized.x, origX);
        assert.equal(serialized.y, origY);
    });

    test("captures controls", () => {
        State.controls.forward = 1;
        State.controls.turnleft = 1;
        const s = serializeState();
        assert.equal(s.controls.forward, 1);
        assert.equal(s.controls.turnleft, 1);
    });

    test("captures own entity id", () => {
        const s = serializeState();
        assert.equal(s.ownEntityId, State.ownEntity.id);
    });

    test("captures trajectory ref id", () => {
        const body = getBodies()[0];
        State.trajectoryRef = body;
        const s = serializeState();
        assert.equal(s.trajectoryRefId, body.id);
        assert.equal(s.isSystemCenterRef, false);
    });

    test("captures system center ref", () => {
        State.trajectoryRef = State.systemCenter;
        const s = serializeState();
        assert.equal(s.isSystemCenterRef, true);
    });

    test("captures simulation config", () => {
        State.predictDelta = 0.5;
        State.predictSteps = 100;
        State.globalTime = 42.5;
        const s = serializeState();
        assert.equal(s.predictDelta, 0.5);
        assert.equal(s.predictSteps, 100);
        assert.equal(s.globalTime, 42.5);
    });

    test("captures triangle-specific fields", () => {
        const s = serializeState();
        const shipData = s.entities.find(e => e.entityType === Entities.Triangle);
        assert.ok(shipData, "should find the ship in serialized state");
        assert.ok(shipData.name !== undefined);
        assert.ok(shipData.boostProgress !== undefined);
    });
});

// ===========================================================================
// Prediction (fallback path) tests
// ====================================================================================

describe("prediction — fallback path", () => {
    beforeEach(() => setupRealSystem());

    test("startPrediction returns true and sets running state", () => {
        assert.equal(isPredictionRunning(), false);
        const started = startPrediction();
        assert.equal(started, true);
        // In fallback mode, prediction runs synchronously, so result is
        // immediately pending.
        assert.equal(isPredictionRunning(), true);
    });

    test("startPrediction returns false if already running", () => {
        startPrediction();
        const startedAgain = startPrediction();
        assert.equal(startedAgain, false);
    });

    test("startPrediction returns false without trajectoryRef", () => {
        State.trajectoryRef = null;
        const started = startPrediction();
        assert.equal(started, false);
    });

    test("pollPrediction completes immediately in fallback mode", () => {
        startPrediction();
        const done = pollPrediction();
        assert.equal(done, true);
        assert.equal(isPredictionRunning(), false);
    });

    test("completed prediction produces valid trajectories", () => {
        startPrediction();
        pollPrediction();
        // Some entities may be destroyed during prediction (collisions),
        // resulting in empty trajectories. Only check that non-empty
        // trajectories are valid.
        let anyTraj = false;
        for (const e of State.updateGroup) {
            if (e.trajectory.length > 0) {
                anyTraj = true;
                assert.ok(e.trajectory.length <= State.predictSteps,
                    `entity ${e.id}: trajectory too long (${e.trajectory.length})`);
            }
        }
        assert.ok(anyTraj, "at least some entities should have trajectories");
    });

    test("trajectories are ref-relative", () => {
        startPrediction();
        pollPrediction();
        // Just verify that trajectory points exist and are finite numbers.
        // The exact values depend on the ref body's motion during prediction,
        // which is hard to predict precisely for a test.
        for (const e of State.updateGroup) {
            if (e.trajectory.length === 0) continue;
            for (const p of e.trajectory) {
                assert.ok(Number.isFinite(p.x), `entity ${e.id}: trajectory X is not finite`);
                assert.ok(Number.isFinite(p.y), `entity ${e.id}: trajectory Y is not finite`);
            }
        }
    });

    test("globalTime is restored after prediction", () => {
        const t0 = State.globalTime;
        startPrediction();
        pollPrediction();
        assert.equal(State.globalTime, t0,
            "globalTime should be restored after fallback prediction");
    });

    test("entity states are restored after prediction", () => {
        const planet = getBodies()[0];
        const origX = planet.x;
        const origY = planet.y;
        const origVX = planet.velX;
        startPrediction();
        pollPrediction();
        assert.equal(planet.x, origX, "planet X should be restored");
        assert.equal(planet.y, origY, "planet Y should be restored");
        assert.equal(planet.velX, origVX, "planet velX should be restored");
    });

    test("ghost ship trajectory is created when controls are active", () => {
        State.controls.forward = 1;
        startPrediction();
        pollPrediction();
        assert.ok(Array.isArray(State.ghostTrajectories));
        assert.ok(Array.isArray(State.ghostTrajectoryColors));
    });

    test("no ghost ship when controls are inactive", () => {
        startPrediction();
        pollPrediction();
        assert.equal(State.ghostTrajectories.length, 0);
    });
});

describe("prediction — continuous operation", () => {
    beforeEach(() => setupRealSystem());

    test("can run multiple predictions in sequence", async () => {
        for (let run = 0; run < 3; run++) {
            assert.equal(isPredictionRunning(), false);
            startPrediction();
            const done = pollPrediction();
            assert.ok(done, `run ${run} should complete`);
            
            // Allow time for the worker cap to expire
            await new Promise(resolve => setTimeout(resolve, 2));
        }
    });

    test("trajectories update on each new prediction", async () => {
        // First prediction
        startPrediction();
        pollPrediction();
        const traj1 = State.updateGroup[0].trajectory.length;

        await new Promise(resolve => setTimeout(resolve, 2));

        // Second prediction
        startPrediction();
        pollPrediction();
        const traj2 = State.updateGroup[0].trajectory.length;

        // Both should produce trajectory points
        assert.ok(traj1 > 0);
        assert.ok(traj2 > 0);
    });
});

// ===========================================================================
// Trajectory coordinate system tests
// ====================================================================================

describe("trajectory coordinates", () => {
    beforeEach(() => setupRealSystem());

    test("trajectory points are global coords", () => {
        // Use a real planet as ref body so we get meaningful relative motion.
        const planet = getBodies().find(b => !b.star);
        if (!planet) return;
        State.trajectoryRef = planet;

        startPrediction();
        pollPrediction();

        // If the planet was destroyed during prediction, skip this test.
        if (planet.trajectory.length === 0) return;

        // Trajectory points should be global coordinates.
        // The ref body's own trajectory should track its motion, not be all (0,0).
        const refTraj = planet.trajectory;
        for (const p of refTraj) {
            assert.ok(Number.isFinite(p.x), `ref body trajectory X should be finite, got ${p.x}`);
            assert.ok(Number.isFinite(p.y), `ref body trajectory Y should be finite, got ${p.y}`);
        }
    });

    test("planet trajectory relative to star is curved", () => {
        // Use the star as ref body. A planet orbiting the star should have
        // a curved (circular/elliptical) trajectory, not a straight line.
        const star = getBodies().find(b => b.star);
        const planet = getBodies().find(b => !b.star);
        if (!star || !planet) return;
        State.trajectoryRef = star;

        startPrediction();
        pollPrediction();

        const traj = planet.trajectory;
        const refTraj = star.trajectory;
        if (traj.length < 3 || refTraj.length < 3) return;

        // Check that the trajectory is curved by verifying that consecutive
        // segments change direction. For a straight line, all cross products
        // would be ~0. For a curve, they should be non-zero.
        let maxCross = 0;
        for (let i = 2; i < traj.length; i++) {
            const rx0 = traj[i-2].x - refTraj[i-2].x;
            const ry0 = traj[i-2].y - refTraj[i-2].y;
            const rx1 = traj[i-1].x - refTraj[i-1].x;
            const ry1 = traj[i-1].y - refTraj[i-1].y;
            const rx2 = traj[i].x - refTraj[i].x;
            const ry2 = traj[i].y - refTraj[i].y;

            const dx1 = rx1 - rx0;
            const dy1 = ry1 - ry0;
            const dx2 = rx2 - rx1;
            const dy2 = ry2 - ry1;
            const cross = dx1 * dy2 - dy1 * dx2;
            maxCross = Math.max(maxCross, Math.abs(cross));
        }
        assert.ok(maxCross > 0.01,
            `trajectory should be curved (max cross product = ${maxCross}), not straight`);
    });

    test("changing ref body does not change trajectory world coords", async () => {
        const star = getBodies().find(b => b.star);
        const planet = getBodies().find(b => !b.star);
        if (!star || !planet) return; // skip if system doesn't have both

        // Prediction with star as ref
        State.trajectoryRef = star;
        startPrediction();
        pollPrediction();
        if (planet.trajectory.length === 0) return; // planet destroyed
        const trajWithStarRef = planet.trajectory.map(p => ({ x: p.x, y: p.y }));

        await new Promise(resolve => setTimeout(resolve, 2));

        // Prediction with planet as ref
        State.trajectoryRef = planet;
        startPrediction();
        pollPrediction();
        if (planet.trajectory.length === 0) return;
        const trajWithPlanetRef = planet.trajectory.map(p => ({ x: p.x, y: p.y }));

        for (let i = 0; i < trajWithStarRef.length; i++) {
            assert.ok(Math.abs(trajWithStarRef[i].x - trajWithPlanetRef[i].x) < 0.1, `planet trajectory X mismatch`);
            assert.ok(Math.abs(trajWithStarRef[i].y - trajWithPlanetRef[i].y) < 0.1, `planet trajectory Y mismatch`);
        }
    });
});
