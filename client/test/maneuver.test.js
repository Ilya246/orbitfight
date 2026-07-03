// Quick test for the maneuver module.
// Run with: node --test client/test/maneuver.test.js

import { test, describe } from "node:test";
import assert from "node:assert/strict";
import {
    ManeuverNode, ManeuverScheduler, ManeuverOrientation,
    findClosestTrajectoryIndexDrawn,
    estimateVelocityAt, findNodeRefTrajectory,
    getManeuverPanelPos, getManeuverButtons, hitTestManeuverButtons,
    spawnManeuverGhosts, stepManeuverGhosts, collectManeuverGhosts,
    resolveRefBody, maneuverColor,
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

    test("computeDeltaV removed — heading tested directly", () => {
        const node = new ManeuverNode(100);
        node.dvMagnitude = 50;
        node.orientation = ManeuverOrientation.Prograde;
        const shipState = { x: 0, y: 0, velX: 10, velY: 0 };
        const heading = node.computeHeading(shipState, null);
        assert.ok(Math.abs(Math.cos(heading) - 1) < 0.001);
    });

    test("computeHeading prograde relative to ref body velocity", () => {
        const node = new ManeuverNode(100);
        node.orientation = ManeuverOrientation.Prograde;
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
        node.frozen = true;
        node.frozenOffset = 30.0;
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
        assert.equal(restored.frozen, true);
        assert.equal(restored.frozenOffset, 30.0);
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
        const manual = new ManeuverNode(50);
        manual.autoExecute = false;
        manual.dvMagnitude = 50;
        sched.addNode(manual);
        const auto = new ManeuverNode(50);
        auto.autoExecute = true;
        auto.dvMagnitude = 50;
        sched.addNode(auto);
        sched.addNode(new ManeuverNode(200));
        sched.cleanupPastNodes(100, 120);
        assert.equal(sched.nodes.length, 2, 'manual node should be removed, auto+future remain');
        assert.ok(!sched.nodes.includes(manual), 'manual node should be gone');
    });

    test("cleanupPastNodes keeps manual node within grace period", () => {
        const sched = new ManeuverScheduler();
        const manual = new ManeuverNode(100);
        manual.autoExecute = false;
        manual.dvMagnitude = 50;
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
    test("findClosestTrajectoryIndexDrawn with no ref body", () => {
        const traj = [
            { x: 0, y: 0 },
            { x: 10, y: 0 },
            { x: 20, y: 0 },
        ];
        assert.equal(findClosestTrajectoryIndexDrawn(traj, null, 0, 0, 12, 0), 1);
        assert.equal(findClosestTrajectoryIndexDrawn(traj, null, 0, 0, 25, 0), 2);
    });

    test("findClosestTrajectoryIndexDrawn with ref body offset", () => {
        const traj = [
            { x: 100, y: 0 },
            { x: 110, y: 0 },
            { x: 120, y: 0 },
        ];
        const refTraj = [
            { x: 1000, y: 0 },
            { x: 1005, y: 0 },
            { x: 1010, y: 0 },
        ];
        const refX = 1000, refY = 0;
        assert.equal(findClosestTrajectoryIndexDrawn(traj, refTraj, refX, refY, 105, 0), 1);
        assert.equal(findClosestTrajectoryIndexDrawn(traj, refTraj, refX, refY, 100, 0), 0);
        assert.equal(findClosestTrajectoryIndexDrawn(traj, refTraj, refX, refY, 110, 0), 2);
    });

    test("estimateVelocityAt central difference", () => {
        const traj = [
            { x: 0, y: 0 },
            { x: 10, y: 0 },
            { x: 20, y: 0 },
        ];
        const v = estimateVelocityAt(traj, 1, 0.5);
        // (20 - 0) / (2 * 0.5) = 20
        assert.ok(Math.abs(v.velX - 20) < 0.001);
        assert.ok(Math.abs(v.velY - 0) < 0.001);
    });

    test("estimateVelocityAt forward difference at start", () => {
        const traj = [
            { x: 0, y: 0 },
            { x: 10, y: 0 },
        ];
        const v = estimateVelocityAt(traj, 0, 0.5);
        // (10 - 0) / 0.5 = 20
        assert.ok(Math.abs(v.velX - 20) < 0.001);
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
        // 6 orient + 1 manual + 2 dv + 2 time + 1 freeze + 1 auto + 1 delete = 14
        assert.equal(btns.length, 14);
        const orientBtns = btns.filter(b => b.action === 'orient');
        assert.equal(orientBtns.length, 6);
        const dvBtns = btns.filter(b => b.action.startsWith('dv_'));
        assert.equal(dvBtns.length, 2);
        const timeBtns = btns.filter(b => b.action.startsWith('t_'));
        assert.equal(timeBtns.length, 2);
        assert.ok(btns.some(b => b.action === 'toggle_freeze'));
    });

    test("hitTestManeuverButtons uses pre-computed panel positions", () => {
        const sched = new ManeuverScheduler();
        const node = new ManeuverNode(100);
        sched.addNode(node);
        const panels = [{ x: 50, y: 50, w: 128, h: 78 }];
        const hit = hitTestManeuverButtons(sched, { x: 70, y: 60 }, panels);
        assert.ok(hit);
        assert.equal(hit.node, node);
        assert.equal(hit.button.action, 'orient');
        assert.equal(hit.button.value, ManeuverOrientation.Prograde);
        const miss = hitTestManeuverButtons(sched, { x: 500, y: 500 }, panels);
        assert.equal(miss, null);
    });
});

describe("maneuverColor", () => {
    test("returns different colors for different indices", () => {
        const c0 = maneuverColor(0);
        const c1 = maneuverColor(1);
        assert.notDeepEqual(c0, c1);
    });
    test("cycles colors", () => {
        const colors = [];
        for (let i = 0; i < 8; i++) colors.push(maneuverColor(i));
        // Color at index 6 should equal color at index 0 (6 colors in palette)
        assert.deepEqual(colors[6], colors[0]);
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
        node.refBodyId = 99999;
        const ref = resolveRefBody(node);
        assert.equal(ref, fallback);
    });
});

describe("maneuver ghosts (per-node chaining)", () => {
    test("spawnManeuverGhosts creates one ghost per node", () => {
        fullClear(true);
        State.updateGroup = [];
        State.simCleanupBuffer = [];
        State.nextID = 0;
        const ship = new Triangle();
        ship.x = 100; ship.y = 200;
        ship.velX = 10; ship.velY = 0;

        const nodes = [
            { id: 1, burnTime: 100.8, dvMagnitude: 50, orientation: 'prograde', manualAngle: 0, autoExecute: false, burnDVApplied: 0 },
            { id: 2, burnTime: 200.8, dvMagnitude: 50, orientation: 'prograde', manualAngle: 0, autoExecute: false, burnDVApplied: 0 },
        ];
        const ghosts = spawnManeuverGhosts(nodes, ship);
        assert.equal(ghosts.length, 2);
        assert.equal(ghosts[0].planNodes.length, 1); // ghost 0 executes node 0 only
        assert.equal(ghosts[1].planNodes.length, 2); // ghost 1 executes nodes 0 and 1
        assert.equal(ghosts[0].maneuverNodeId, 1);
        assert.equal(ghosts[1].maneuverNodeId, 2);
        assert.equal(ghosts[0].phantom, true);
        assert.equal(ghosts[1].phantom, true);
    });

    test("stepManeuverGhosts applies burns at burn time", () => {
        fullClear(true);
        State.updateGroup = [];
        State.simCleanupBuffer = [];
        State.nextID = 0;
        State.delta = 0.1;
        State.simulating = true;
        State.authority = true;

        const ship = new Triangle();
        ship.x = 0; ship.y = 0;
        ship.velX = 100; ship.velY = 0;
        ship.rotation = 0;

        const startGlobalTime = 1000;
        const predictDelta = 0.1;
        const burnTime = startGlobalTime + 5 * predictDelta;
        const nodes = [{
            id: 1, burnTime: burnTime, dvMagnitude: 50, orientation: 'prograde',
            manualAngle: 0, autoExecute: false, burnDVApplied: 0,
        }];
        const ghosts = spawnManeuverGhosts(nodes, ship);
        const ghost = ghosts[0];

        // Before burn: recording should be false
        stepManeuverGhosts(startGlobalTime + predictDelta, predictDelta);
        assert.equal(ghost.recording, false);

        // At burn time: recording should be true
        stepManeuverGhosts(burnTime, predictDelta);
        assert.equal(ghost.recording, true);
    });

    test("collectManeuverGhosts returns ghost data", () => {
        fullClear(true);
        State.updateGroup = [];
        State.simCleanupBuffer = [];
        State.nextID = 0;
        State.delta = 0.1;
        State.simulating = true;
        State.authority = true;

        const ship = new Triangle();
        ship.x = 0; ship.y = 0;
        ship.velX = 100; ship.velY = 0;
        ship.rotation = 0;

        const startGlobalTime = 1000;
        const predictDelta = 0.1;
        const burnTime = startGlobalTime + 5 * predictDelta;
        const nodes = [{
            id: 1, burnTime: burnTime, dvMagnitude: 50, orientation: 'prograde',
            manualAngle: 0, autoExecute: false, burnDVApplied: 0,
        }];
        spawnManeuverGhosts(nodes, ship);

        // Run a few steps
        for (let i = 0; i < 10; i++) {
            stepManeuverGhosts(startGlobalTime + (i + 1) * predictDelta, predictDelta);
        }

        const collected = collectManeuverGhosts(startGlobalTime, predictDelta);
        assert.equal(collected.length, 1);
        assert.equal(collected[0].nodeId, 1);
        assert.ok(collected[0].startIndex >= 0);
    });
});
