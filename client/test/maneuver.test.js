// Quick test for the maneuver module.
// Run with: node --test client/test/maneuver.test.js

import { test, describe } from "node:test";
import assert from "node:assert/strict";
import {
    ManeuverNode, ManeuverScheduler, ManeuverOrientation,
    findClosestTrajectoryIndex, findClosestTrajectoryIndexDrawn,
    getManeuverNodePosition,
    getManeuverPanelPos, getManeuverButtons, hitTestManeuverButtons,
    createManeuverGhost, updateManeuverGhost, resolveRefBody,
    GHOST_PREVIEW_COLOR, GHOST_EXECUTING_COLOR,
} from "../maneuver.js";
import {
    State, Triangle, CelestialBody, fullClear,
} from "../engine.js";

describe("ManeuverNode", () => {
    test("computeHeading prograde", () => {
        const node = new ManeuverNode(100);
        node.orientation = ManeuverOrientation.Prograde;
        const shipState = { x: 0, y: 0, velX: 10, velY: 0 };
        const heading = node.computeHeading(shipState, null);
        assert.ok(Math.abs(heading - 0) < 0.001, `prograde heading should be 0, got ${heading}`);
    });

    test("computeHeading retrograde", () => {
        const node = new ManeuverNode(100);
        node.orientation = ManeuverOrientation.Retrograde;
        const shipState = { x: 0, y: 0, velX: 10, velY: 0 };
        const heading = node.computeHeading(shipState, null);
        // atan2(-0, -10) = -PI, which is the same direction as PI
        const normalized = Math.abs(heading) / Math.PI;
        assert.ok(Math.abs(normalized - 1) < 0.001, `retrograde heading should be ±PI, got ${heading}`);
    });

    test("computeHeading manual", () => {
        const node = new ManeuverNode(100);
        node.orientation = ManeuverOrientation.Manual;
        node.manualAngle = 1.5;
        const shipState = { x: 0, y: 0, velX: 10, velY: 0 };
        const heading = node.computeHeading(shipState, null);
        assert.ok(Math.abs(heading - 1.5) < 0.001, `manual heading should be 1.5, got ${heading}`);
    });

    test("computeDeltaV", () => {
        const node = new ManeuverNode(100);
        node.dvMagnitude = 50;
        node.orientation = ManeuverOrientation.Prograde;
        const shipState = { x: 0, y: 0, velX: 10, velY: 0 };
        const dv = node.computeDeltaV(shipState, null);
        assert.ok(Math.abs(dv.x - 50) < 0.001);
        assert.ok(Math.abs(dv.y - 0) < 0.001);
    });

    test("computeHeading prograde relative to ref body velocity", () => {
        const node = new ManeuverNode(100);
        node.orientation = ManeuverOrientation.Prograde;
        // Ship moves at (15, 0), ref body moves at (5, 0)
        // Relative velocity = (10, 0), so prograde heading = 0
        const shipState = { x: 0, y: 0, velX: 15, velY: 0 };
        const refBody = { x: 100, y: 0, velX: 5, velY: 0 };
        const heading = node.computeHeading(shipState, refBody);
        assert.ok(Math.abs(heading - 0) < 0.001, `relative prograde heading should be 0, got ${heading}`);
    });

    test("computeHeading radial in toward ref body position", () => {
        const node = new ManeuverNode(100);
        node.orientation = ManeuverOrientation.RadialIn;
        const shipState = { x: 0, y: 0, velX: 10, velY: 0 };
        const refBody = { x: 100, y: 0, velX: 5, velY: 0 };
        const heading = node.computeHeading(shipState, refBody);
        // Toward ref body from origin = atan2(0, 100) = 0
        assert.ok(Math.abs(heading - 0) < 0.001, `radial in heading should be 0, got ${heading}`);
    });

    test("serialize/deserialize roundtrip", () => {
        const node = new ManeuverNode(100);
        node.dvMagnitude = 75;
        node.orientation = ManeuverOrientation.Retrograde;
        node.manualAngle = 2.0;
        node.autoExecute = true;
        node.worldX = 1234.5;
        node.worldY = -6789.0;
        node.initialVelX = 10.5;
        node.initialVelY = -20.5;
        node.refBodyId = 42;
        node.refBodyIsSystemCenter = true;
        const s = node.serialize();
        const restored = ManeuverNode.deserialize(s);
        assert.equal(restored.burnTime, 100);
        assert.equal(restored.dvMagnitude, 75);
        assert.equal(restored.orientation, ManeuverOrientation.Retrograde);
        assert.equal(restored.manualAngle, 2.0);
        assert.equal(restored.autoExecute, true);
        assert.equal(restored.worldX, 1234.5);
        assert.equal(restored.worldY, -6789.0);
        assert.equal(restored.initialVelX, 10.5);
        assert.equal(restored.initialVelY, -20.5);
        assert.equal(restored.refBodyId, 42);
        assert.equal(restored.refBodyIsSystemCenter, true);
    });
});

describe("ManeuverScheduler", () => {
    test("addNode sorts by burnTime", () => {
        const sched = new ManeuverScheduler();
        const n1 = new ManeuverNode(200);
        const n2 = new ManeuverNode(100);
        const n3 = new ManeuverNode(150);
        sched.addNode(n1);
        sched.addNode(n2);
        sched.addNode(n3);
        const sorted = sched.getSortedNodes();
        assert.equal(sorted[0], n2);
        assert.equal(sorted[1], n3);
        assert.equal(sorted[2], n1);
    });

    test("removeNode", () => {
        const sched = new ManeuverScheduler();
        const n1 = new ManeuverNode(100);
        const n2 = new ManeuverNode(200);
        sched.addNode(n1);
        sched.addNode(n2);
        sched.removeNode(n1);
        assert.equal(sched.nodes.length, 1);
        assert.equal(sched.nodes[0], n2);
    });

    test("cleanupPastNodes removes manual nodes after burn window", () => {
        const sched = new ManeuverScheduler();
        // Manual node whose burn window has passed (burnTime=50, burnDuration≈0.52, +2s grace → ~52.5)
        const manual = new ManeuverNode(50);
        manual.autoExecute = false;
        manual.dvMagnitude = 50;
        sched.addNode(manual);
        // Auto node in the past (kept by threshold)
        const auto = new ManeuverNode(50);
        auto.autoExecute = true;
        auto.dvMagnitude = 50;
        sched.addNode(auto);
        // Future node
        sched.addNode(new ManeuverNode(200));
        // At time 100: manual node should be gone (100 > 52.5), auto stays (within 120s), future stays
        sched.cleanupPastNodes(100, 120);
        assert.equal(sched.nodes.length, 2, 'manual node should be removed, auto+future remain');
        assert.ok(!sched.nodes.includes(manual), 'manual node should be gone');
    });

    test("cleanupPastNodes keeps manual node within grace period", () => {
        const sched = new ManeuverScheduler();
        const manual = new ManeuverNode(100);
        manual.autoExecute = false;
        manual.dvMagnitude = 50; // burnDuration ≈ 0.52s, grace until ~102.5
        sched.addNode(manual);
        sched.cleanupPastNodes(101, 120);
        assert.equal(sched.nodes.length, 1, 'manual node should still be within grace');
    });

    test("serialize includes nodes and defaultAutoExecute", () => {
        const sched = new ManeuverScheduler();
        sched.defaultAutoExecute = true;
        sched.addNode(new ManeuverNode(100));
        const s = sched.serialize();
        assert.equal(s.nodes.length, 1);
        assert.equal(s.defaultAutoExecute, true);
    });
});

describe("trajectory helpers", () => {
    test("findClosestTrajectoryIndex", () => {
        const traj = [
            { x: 0, y: 0 },
            { x: 10, y: 0 },
            { x: 20, y: 0 },
        ];
        assert.equal(findClosestTrajectoryIndex(traj, 12, 0), 1);
        assert.equal(findClosestTrajectoryIndex(traj, 25, 0), 2);
    });

    test("findClosestTrajectoryIndexDrawn with no ref body", () => {
        const traj = [
            { x: 0, y: 0 },
            { x: 10, y: 0 },
            { x: 20, y: 0 },
        ];
        // No ref trajectory — should behave like findClosestTrajectoryIndex
        assert.equal(findClosestTrajectoryIndexDrawn(traj, null, 0, 0, 12, 0), 1);
        assert.equal(findClosestTrajectoryIndexDrawn(traj, null, 0, 0, 25, 0), 2);
    });

    test("findClosestTrajectoryIndexDrawn with ref body offset", () => {
        // Ship trajectory in absolute coords
        const traj = [
            { x: 100, y: 0 },
            { x: 110, y: 0 },
            { x: 120, y: 0 },
        ];
        // Ref body trajectory: ref body is at (1000, 0) at step 0, moves to (1010, 0)
        const refTraj = [
            { x: 1000, y: 0 },
            { x: 1005, y: 0 },
            { x: 1010, y: 0 },
        ];
        // Ref body current position
        const refX = 1000, refY = 0;
        // Drawn position of traj[1] = refX + (110 - 1005) = 1000 + (-895) = 105
        // So clicking at drawX=105 should find index 1
        assert.equal(findClosestTrajectoryIndexDrawn(traj, refTraj, refX, refY, 105, 0), 1);
        // Drawn position of traj[0] = 1000 + (100 - 1000) = 100
        assert.equal(findClosestTrajectoryIndexDrawn(traj, refTraj, refX, refY, 100, 0), 0);
        // Drawn position of traj[2] = 1000 + (120 - 1010) = 110
        assert.equal(findClosestTrajectoryIndexDrawn(traj, refTraj, refX, refY, 110, 0), 2);
    });

    test("getManeuverNodePosition", () => {
        const traj = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 20, y: 0 }];
        const node = new ManeuverNode(100 + 2 * 0.4); // burnTime = 100.8, stepIndex = 1
        const pos = getManeuverNodePosition(node, traj, 100, 0.4);
        assert.ok(pos);
        assert.equal(pos.x, 10);
    });
});

describe("UI layout", () => {
    test("getManeuverPanelPos stays in bounds", () => {
        const pos = getManeuverPanelPos(700, 400, 800, 600);
        assert.ok(pos.x + pos.w <= 800);
        assert.ok(pos.y >= 0);
        assert.ok(pos.y + pos.h <= 600);
    });

    test("getManeuverPanelPos flips to left when no space on right", () => {
        const pos = getManeuverPanelPos(780, 300, 800, 600);
        assert.ok(pos.x < 780);
    });

    test("getManeuverButtons returns all buttons", () => {
        const node = new ManeuverNode(100);
        const btns = getManeuverButtons(node, 100, 100);
        // 6 orientation + 1 manual + 4 dv + 1 auto + 1 delete = 13
        assert.equal(btns.length, 13);
        // Check that orient buttons have value
        const orientBtns = btns.filter(b => b.action === 'orient');
        assert.equal(orientBtns.length, 6);
        // Check dv buttons
        const dvBtns = btns.filter(b => b.action.startsWith('dv_'));
        assert.equal(dvBtns.length, 4);
    });

    test("hitTestManeuverButtons uses pre-computed panel positions", () => {
        const sched = new ManeuverScheduler();
        const node = new ManeuverNode(100);
        sched.addNode(node);
        const panels = [{ x: 50, y: 50, w: 128, h: 78 }];
        // Click inside a button (Pro button at panelX+4, panelY+4, 38x16)
        const hit = hitTestManeuverButtons(sched, { x: 70, y: 60 }, panels);
        assert.ok(hit);
        assert.equal(hit.node, node);
        assert.equal(hit.button.action, 'orient');
        assert.equal(hit.button.value, ManeuverOrientation.Prograde);
        // Click outside all panels
        const miss = hitTestManeuverButtons(sched, { x: 500, y: 500 }, panels);
        assert.equal(miss, null);
    });
});

describe("maneuver ghosts", () => {
    test("createManeuverGhost creates ghost", () => {
        fullClear(true);
        State.updateGroup = [];
        State.simCleanupBuffer = [];
        State.nextID = 0;
        const ship = new Triangle();
        ship.x = 100; ship.y = 200;
        ship.velX = 10; ship.velY = 0;

        const nodes = [{
            id: 1, burnTime: 100.8, dvMagnitude: 50,
            orientation: 'prograde', manualAngle: 0, autoExecute: false,
        }];
        const ghost = createManeuverGhost(ship, nodes);
        assert.deepEqual(ghost.color, GHOST_EXECUTING_COLOR);
    });

    test("ghost recording flag set on burn start", () => {
        fullClear(true);
        State.updateGroup = [];
        State.simCleanupBuffer = [];
        State.nextID = 0;
        State.delta = 0.4;
        State.simulating = true;
        State.authority = true;
        State.controls = { forward:0, backward:0, turnleft:0, turnright:0, boost:0, slowrotate:0, primaryfire:0, secondaryfire:0 };

        const ship = new Triangle();
        ship.x = 0; ship.y = 0;
        ship.velX = 100; ship.velY = 0;
        ship.rotation = 0;

        const startGlobalTime = 1000;
        const predictDelta = 0.4;
        const burnTime = startGlobalTime + 3 * predictDelta;
        const nodes = [{
            id: 1, burnTime: burnTime, dvMagnitude: 50,
            orientation: 'prograde', manualAngle: 0, autoExecute: false,
        }];
        const ghost = createManeuverGhost(ship, 'preview', nodes);
        const zeroControls = { forward:0, backward:0, turnleft:0, turnright:0, boost:0, slowrotate:0, primaryfire:0, secondaryfire:0 };

        // Before burn step: recording should be false
        updateManeuverGhost(ghost, 0, startGlobalTime, predictDelta, zeroControls);
        assert.equal(ghost.recording, false);

        // At burn step (step 2): recording should be true
        updateManeuverGhost(ghost, 1, startGlobalTime, predictDelta, zeroControls);
        updateManeuverGhost(ghost, 2, startGlobalTime, predictDelta, zeroControls);
        assert.equal(ghost.recording, true);
    });

    test("executing ghost accumulates thrust delta-V for completion", () => {
        fullClear(true);
        State.updateGroup = [];
        State.simCleanupBuffer = [];
        State.nextID = 0;
        State.delta = 0.4;
        State.simulating = true;
        State.authority = true;
        State.controls = { forward:0, backward:0, turnleft:0, turnright:0, boost:0, slowrotate:0, primaryfire:0, secondaryfire:0 };

        const ship = new Triangle();
        ship.x = 0; ship.y = 0;
        ship.velX = 100; ship.velY = 0;
        ship.rotation = 0; // aligned with prograde (velX positive)

        const startGlobalTime = 1000;
        const predictDelta = 0.4;
        const dvMag = 96; // exactly 1 second of thrust at accel=96
        const burnDuration = dvMag / 96; // 1.0s
        const burnTime = startGlobalTime + 5 * predictDelta; // burn center at step 5
        const nodes = [{
            id: 1, burnTime: burnTime, dvMagnitude: dvMag,
            orientation: 'prograde', manualAngle: 0, autoExecute: false,
        }];
        const ghost = createManeuverGhost(ship, 'executing', nodes);
        const zeroControls = { forward:0, backward:0, turnleft:0, turnright:0, boost:0, slowrotate:0, primaryfire:0, secondaryfire:0 };

        // Burn starts at burnTime - burnDuration/2 = burnTime - 0.5
        // = startGlobalTime + 5*0.4 - 0.5 = startGlobalTime + 1.5
        // stepGlobalTime at step i = startGlobalTime + (i+1)*0.4
        // burn starts when stepGlobalTime >= startGlobalTime + 1.5, i.e., (i+1)*0.4 >= 1.5, i >= 2.75, so i=3
        // Since ship is already aligned (rotation=0, prograde=0), it thrusts immediately.
        // Each thrust step adds 96*0.4 = 38.4 to burnDVApplied.
        // To reach 96, need ceil(96/38.4) = 3 thrust steps.
        for (let i = 0; i < 10; i++) {
            updateManeuverGhost(ghost, i, startGlobalTime, predictDelta, zeroControls);
        }
        // Burn should have completed (burnDVApplied >= 96)
        assert.ok(ghost.burnDVApplied >= dvMag || ghost.currentBurn === null,
            `burn should complete: burnDVApplied=${ghost.burnDVApplied}, currentBurn=${ghost.currentBurn}`);
    });
});

describe("resolveRefBody", () => {
    test("resolves system center when refBodyIsSystemCenter is true", () => {
        fullClear(true);
        State.updateGroup = [];
        State.nextID = 0;
        State.systemCenter = new CelestialBody(true);
        const node = new ManeuverNode(100);
        node.refBodyIsSystemCenter = true;
        const ref = resolveRefBody(node);
        assert.equal(ref, State.systemCenter);
    });

    test("resolves entity by ID from updateGroup", () => {
        fullClear(true);
        State.updateGroup = [];
        State.nextID = 0;
        State.systemCenter = new CelestialBody(true);
        const body = new CelestialBody(100, 1e18);
        const node = new ManeuverNode(100);
        node.refBodyIsSystemCenter = false;
        node.refBodyId = body.id;
        const ref = resolveRefBody(node);
        assert.equal(ref, body);
    });

    test("falls back to trajectoryRef when refBodyId not found", () => {
        fullClear(true);
        State.updateGroup = [];
        State.nextID = 0;
        State.systemCenter = new CelestialBody(true);
        const fallback = new CelestialBody(50, 1e18);
        State.trajectoryRef = fallback;
        const node = new ManeuverNode(100);
        node.refBodyIsSystemCenter = false;
        node.refBodyId = 99999; // doesn't exist
        const ref = resolveRefBody(node);
        assert.equal(ref, fallback);
    });
});
