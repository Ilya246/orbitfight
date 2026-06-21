// Ported from include/globals.hpp, include/entities.hpp, include/camera.hpp,
// src/entities.cpp, src/camera.cpp.

import { PI, TAU, degToRad, dst, dst2, rand_f, deltaAngleRad, C, CC } from "./math.js";
import { Entities, Types } from "./types.js";

// ============================================================================
// Global state (ported from globals.hpp)
// ============================================================================

export function zeroMovement() {
    return {
        forward: 0, backward: 0, turnright: 0, turnleft: 0,
        boost: 0, slowrotate: 0, primaryfire: 0, secondaryfire: 0,
    };
}

export function movementEqual(a, b) {
    return (
        a.forward === b.forward &&
        a.backward === b.backward &&
        a.turnright === b.turnright &&
        a.turnleft === b.turnleft &&
        a.boost === b.boost &&
        a.slowrotate === b.slowrotate &&
        a.primaryfire === b.primaryfire &&
        a.secondaryfire === b.secondaryfire
    );
}

export const State = {
    ownEntity: null,
    serverSocket: null,
    updateGroup: [],
    simCleanupBuffer: [],
    ghostTrajectories: [],
    ghostTrajectoryColors: [],
    quadtree: [],

    mousePos: { x: 0, y: 0 },
    mouseScreen: { x: 0, y: 0 },

    name: "Player",
    lastControls: zeroMovement(),
    controls: zeroMovement(),

    delta: 1.0 / 60.0,
    globalTime: 0.0,
    deltaOverride: -1.0,
    timescale: 1.0,

    projectileSweepSpacing: 30.0,
    collideRestitution: 1.6,
    friction: 0.002,

    gen_extraStarChance: 0.3,
    gen_blackholeChance: 1.0 / 3.0,
    gen_chandrasekharLimit: 2e26,
    gen_starMass: 4.0e22,
    gen_starRadius: 4.5e4,
    gen_firstPlanetDistance: 3.0e4,
    gen_minNextRadius: 1.15,
    gen_maxNextRadius: 1.33,
    gen_minPlanetRadius: 300.0,
    gen_maxPlanetRadius: 18000.0,
    gen_starMassReq: 0.1,
    gen_baseDensity: 2.0e9,
    gen_densityFactor: 3.0,
    gen_starDensityFactor: 1.25,
    gen_starColorFactor: 0.25,
    gen_moonFactor: 18000.0 * 0.2,
    gen_moonPower: 1.5,
    gen_minMoonDistance: 1.5,
    gen_maxMoonDistance: 9.0,
    gen_minMoonRadius: 120.0,
    gen_maxMoonRadiusFrac: 1.0 / 6.0,

    shipSpawnDistanceMin: 1.4,
    shipSpawnDistanceMax: 3.0,

    sweepThreshold: 3.0e6 * 3.0e6,

    predictSpacing: 0.25,
    predictDelta: 0.4,
    predictBaseScale: 200.0,

    G: 6.67e-11,
    gravityAccuracy: 5.0,
    targetFramerate: 90.0,

    lastPing: 0.0,
    lastPredict: 0.0,
    lastSweep: 0.0,
    predictingFor: 0.0,

    drawShiftX: 0.0,
    drawShiftY: 0.0,
    ownX: 0.0,
    ownY: 0.0,

    textCharacterSize: 18,
    nextID: 0,
    predictSteps: Math.floor(90.0 / 0.4),
    gen_baseMinPlanets: 10,
    gen_baseMaxPlanets: 15,

    messageLimit: 50,
    usernameLimit: 24,

    measureFrames: 0,
    framerate: 0,
    trajectoryOffset: 0,

    authority: true,
    debug: false,
    lockControls: false,
    handledTextBoxSelect: false,
    enableControlLock: false,
    simulating: false,

    trajectoryAlpha: 160,
    worldBrightness: 32,
    worldBrightnessMax: 32,
    worldBrightnessMin: 0,

    trajectoryRef: null,
    lastTrajectoryRef: null,
    systemCenter: null,

    kills: 0,

    vars: {},
};

// ============================================================================
// Camera (ported from camera.hpp / camera.cpp)
// ============================================================================

export class Camera {
    constructor() {
        this.scale = 1;
        this.w = 800;
        this.h = 800;
        this.pos = { x: 0, y: 0 };
    }

    resize(w, h) {
        this.w = w;
        this.h = h;
    }

    zoom(by) {
        this.scale *= by;
    }

    worldToScreenX(x) {
        return this.w * 0.5 + (x - State.ownX) / this.scale;
    }
    worldToScreenY(y) {
        return this.h * 0.5 + (y - State.ownY) / this.scale;
    }
}

export const g_camera = new Camera();

// ============================================================================
// Message log
// ============================================================================

let messageLog = [];
export function getMessages() { return messageLog; }
export function pushMessage(s) {
    messageLog.push(s);
    if (messageLog.length > 200) messageLog.shift();
    for (const l of MessageDisplayedListeners) l(s);
}

export const MessageDisplayedListeners = [];

// ============================================================================
// Entity classes (ported from entities.hpp / entities.cpp)
// ============================================================================

export class Entity {
    constructor() {
        this.id = State.nextID++;
        State.updateGroup.push(this);
        this.ghost = State.simulating;

        this.x = 0; this.y = 0; this.velX = 0; this.velY = 0;
        this.aX = 0; this.aXO = 0; this.aY = 0; this.aYO = 0;
        this.rotation = 0; this.rotateVel = 0;
        this.dVelX = 0; this.dVelY = 0;
        this.radius = 0; this.mass = 0;
        this.resX = 0; this.resY = 0; this.resVelX = 0; this.resVelY = 0;
        this.resAX = 0; this.resAY = 0; this.resRotation = 0; this.resRotateVel = 0;
        this.resMass = 0; this.resRadius = 0;
        this.syncX = 0; this.syncY = 0; this.syncVelX = 0; this.syncVelY = 0;

        this.ai = false;
        this.synced = false;
        this.active = true;
        this.gravitates = false;

        this.simRelBody = null;

        this.color = [255, 255, 255];

        this.parent_id = Number.MAX_SAFE_INTEGER;

        this.trajectory = [];
        this.collided = [];

        this.player = null;
    }

    setPosition(x, y) { this.x = x; this.y = y; }
    setVelocity(x, y) { this.velX = x; this.velY = y; }
    addVelocity(dx, dy) { this.velX += dx; this.velY += dy; }
    addAccel(dx, dy) { this.aX += dx; this.aY += dy; }
    setColor(r, g, b) { this.color = [r, g, b]; }

    update1() {
        this.x += this.velX * State.delta + 0.5 * this.aX * State.delta * State.delta;
        this.y += this.velY * State.delta + 0.5 * this.aY * State.delta * State.delta;
        this.rotation += this.rotateVel * State.delta;
        this.aXO = this.aX;
        this.aYO = this.aY;
        this.aX = 0;
        this.aY = 0;
        this.collided = [];
    }
    update2() {
        if (State.quadtree.length > 0) {
            State.quadtree[0].collideAttract(this, true, true);
        }
    }
    update3() {
        this.velX += 0.5 * (this.aXO + this.aX) * State.delta;
        this.velY += 0.5 * (this.aYO + this.aY) * State.delta;
    }

    collide(with_, specialOnly) {
        if (specialOnly) return;
        if (this.parent_id === with_.id || with_.parent_id === this.id) return;
        const massFactorThis = 1.0 / (1.0 + this.mass / with_.mass);
        const massFactorOther = 1.0 / (1.0 + with_.mass / this.mass);
        const inHeading = Math.atan2(this.y - with_.y, this.x - with_.x);
        const inX = Math.cos(inHeading);
        const inY = Math.sin(inHeading);
        const newX = this.x + (with_.x - this.x + (this.radius + with_.radius) * inX) * massFactorThis;
        const newY = this.y + (with_.y - this.y + (this.radius + with_.radius) * inY) * massFactorThis;
        with_.x -= (with_.x - this.x + (this.radius + with_.radius) * inX) * massFactorOther;
        with_.y -= (with_.y - this.y + (this.radius + with_.radius) * inY) * massFactorOther;
        this.x = newX;
        this.y = newY;
        const dVx = with_.velX - this.velX;
        const dVy = with_.velY - this.velY;
        const velHeading = Math.atan2(dVy, dVx);
        let factor = Math.cos(Math.abs(deltaAngleRad(inHeading, velHeading)));
        if (factor < 0.0) return;
        factor *= dst(dVx, dVy) * State.collideRestitution;
        this.addVelocity(massFactorThis * inX * factor, massFactorThis * inY * factor);
        this.addAccel(massFactorThis * State.friction * dVx, massFactorThis * State.friction * dVy);
        with_.addVelocity(-massFactorOther * inX * factor, -massFactorOther * inY * factor);
        with_.addAccel(-massFactorOther * State.friction * dVx, -massFactorOther * State.friction * dVy);
    }

    simSetup() {
        this.resX = this.x; this.resY = this.y;
        this.resVelX = this.velX; this.resVelY = this.velY;
        this.resAX = this.aX; this.resAY = this.aY;
        this.resRotation = this.rotation; this.resRotateVel = this.rotateVel;
        this.resMass = this.mass; this.resRadius = this.radius;
    }
    simReset() {
        this.x = this.resX; this.y = this.resY;
        this.velX = this.resVelX; this.velY = this.resVelY;
        this.aX = this.resAX; this.aY = this.resAY;
        this.rotation = this.resRotation; this.rotateVel = this.resRotateVel;
        this.mass = this.resMass; this.radius = this.resRadius;
    }

    onEntityDelete(_d) {
        if (this.simRelBody === _d) this.simRelBody = null;
    }

    // Network serialization — overridden by subclasses
    loadCreatePacket(_w) {}
    unloadCreatePacket(_r) {}
    loadSyncPacket(_w) {}
    unloadSyncPacket(_r) {}
}

// ============================================================================
// Quadtree (ported from struct Quad)
// ============================================================================

export class Quad {
    constructor() {
        this.size = 0;
        this.tX = 0; this.tY = 0;
        this.xsize = 0; this.ysize = 0;
        this.x = 0; this.y = 0;
        this.comx = 0; this.comy = 0;
        this.mass = 0;
        this.children = [0, 0, 0, 0];
        this.entity = null;
        this.used = false;
        this.hasGravitators = false;
    }

    static getMakeChild(id, at_x, at_y) {
        const parent = State.quadtree[id];
        const at = (at_x > parent.tX + parent.size * 0.5 ? 1 : 0) + 2 * (at_y > parent.tY + parent.size * 0.5 ? 1 : 0);
        if (parent.children[at] === 0) {
            const next_id = State.quadtree.length;
            State.quadtree.push(new Quad());
            const child = State.quadtree[next_id];
            const halfsize = parent.size * 0.5;
            child.tX = (at === 1 || at === 3) ? parent.tX + halfsize : parent.tX;
            child.x = child.tX;
            child.tY = (at > 1) ? parent.tY + halfsize : parent.tY;
            child.y = child.tY;
            child.size = halfsize;
            child.xsize = halfsize;
            child.ysize = halfsize;
            State.quadtree[id].children[at] = next_id;
            return next_id;
        }
        return parent.children[at];
    }

    getChild(at) { return State.quadtree[this.children[at]]; }

    static put(id, e, reclevel) {
        const cur = State.quadtree[id];
        cur.mass += e.mass;
        cur.comx += e.mass * e.x;
        cur.comy += e.mass * e.y;
        cur.hasGravitators = cur.hasGravitators || e.gravitates;
        if (reclevel > 512) {
            console.warn("quadtree recursion limit hit by entity", e.id);
            e.active = false;
            return;
        }
        const nEntX = e.x + e.dVelX;
        const nEntY = e.y + e.dVelY;
        cur.x = Math.min(cur.x, nEntX - e.radius);
        cur.xsize = Math.max(cur.xsize, nEntX + e.radius - cur.x);
        cur.y = Math.min(cur.y, nEntY - e.radius);
        cur.ysize = Math.max(cur.ysize, nEntY + e.radius - cur.y);
        if (cur.used) {
            const ent = cur.entity;
            Quad.put(Quad.getMakeChild(id, e.x, e.y), e, reclevel + 1);
            if (ent) {
                if (ent.ghost && ent.parent_id === e.id) {
                    State.quadtree[id].entity = null;
                    return;
                }
                Quad.put(Quad.getMakeChild(id, ent.x, ent.y), ent, reclevel + 1);
                State.quadtree[id].entity = null;
            }
        } else {
            cur.entity = e;
            cur.used = true;
        }
    }

    unstaircasize() {
        for (let i = 0; i < this.children.length; i++) {
            const c = this.children[i];
            if (c !== 0) {
                const child = State.quadtree[c];
                const retcode = child.unstaircasize();
                if (child.comx === this.comx && child.comy === this.comy) {
                    return retcode === 0 ? c : retcode;
                } else if (retcode !== 0) {
                    this.children[i] = retcode;
                }
            }
        }
        return 0;
    }

    postBuild() {
        this.comx /= this.mass;
        this.comy /= this.mass;
        for (const c of this.children) {
            if (c !== 0) {
                const child = State.quadtree[c];
                child.postBuild();
                this.x = Math.min(this.x, child.x);
                this.xsize = Math.max(this.xsize, child.x - this.x + child.xsize);
                this.y = Math.min(this.y, child.y);
                this.ysize = Math.max(this.ysize, child.y - this.y + child.ysize);
            }
        }
    }

    collideAttract(e, doGravity, checkCollide) {
        checkCollide = checkCollide &&
        e.x - e.radius - Math.max(e.dVelX, 0.0) < (this.x + this.xsize) &&
        e.y - e.radius - Math.max(e.dVelY, 0.0) < (this.y + this.ysize) &&
        e.x + e.radius + Math.max(e.dVelX, 0.0) > this.x &&
        e.y + e.radius + Math.max(e.dVelX, 0.0) > this.y;

        if (this.entity && this.entity !== e) {
            if (e.parent_id === this.entity.id || this.entity.parent_id === e.id) return;
            if (checkCollide && !e.collided.includes(this.entity.id)) {
                const dVx = this.entity.dVelX - e.dVelX;
                const dVy = this.entity.dVelY - e.dVelY;
                const dx = e.x - this.entity.x;
                const dy = e.y - this.entity.y;
                const radiusSum = e.radius + this.entity.radius;
                if (dst2(dx, dy) <= radiusSum * radiusSum) {
                    e.collide(this.entity, false);
                    this.entity.collide(e, true);
                    this.entity.collided.push(e.id);
                } else if (Math.abs(dx) - radiusSum < Math.abs(dVx) * 2.0 && Math.abs(dy) - radiusSum < Math.abs(dVy) * 2.0) {
                    const vel = dst(dVx, dVy);
                    if (vel > 0) {
                        const ivel = 1.0 / vel;
                        const cApproach = (dx * dVy - dy * dVx) * ivel;
                        const cApproachAt = Math.sqrt(Math.max(0, dst2(dx, dy) - cApproach * cApproach));
                        if (Math.abs(cApproach) <= radiusSum && cApproachAt <= vel) {
                            e.collide(this.entity, false);
                            this.entity.collide(e, true);
                            this.entity.collided.push(e.id);
                        }
                    }
                }
            }
            if (doGravity) {
                const xdiff = this.entity.x - e.x;
                const ydiff = this.entity.y - e.y;
                const dist = dst(xdiff, ydiff);
                if (dist > 0) {
                    const factor = this.entity.mass * State.G / (dist * dist * dist);
                    e.addAccel(xdiff * factor, ydiff * factor);
                }
            }
            return;
        }
        if (doGravity) {
            const xdiff = this.comx - e.x;
            const ydiff = this.comy - e.y;
            if ((!this.hasGravitators || Math.abs(e.x - this.comx) + Math.abs(e.y - this.comy) > State.gravityAccuracy * this.size) && this.entity !== e) {
                const dist = dst(xdiff, ydiff);
                if (dist > 0) {
                    const factor = this.mass * State.G / (dist * dist * dist);
                    e.addAccel(xdiff * factor, ydiff * factor);
                    doGravity = false;
                }
            }
        } else if (!checkCollide) {
            return;
        }
        for (const c of this.children) {
            if (c !== 0) {
                State.quadtree[c].collideAttract(e, doGravity, checkCollide);
            }
        }
    }
}

export function buildQuadtree() {
    let x1 = +Infinity, y1 = +Infinity, x2 = -Infinity, y2 = -Infinity;
    for (const e of State.updateGroup) {
        x1 = Math.min(e.x, x1);
        y1 = Math.min(e.y, y1);
        x2 = Math.max(e.x, x2);
        y2 = Math.max(e.y, y2);
    }
    State.quadtree = [];
    State.quadtree.push(new Quad());
    const root = State.quadtree[0];
    root.x = x1; root.tX = x1;
    root.y = y1; root.tY = y1;
    root.size = Math.max(x2 - x1, y2 - y1);
    root.xsize = x2 - x1;
    root.ysize = y2 - y1;
    for (let i = 0; i < State.updateGroup.length; i++) {
        Quad.put(0, State.updateGroup[i], 0);
    }
    if (State.updateGroup.length !== 0) {
        State.quadtree[0].unstaircasize();
        State.quadtree[0].postBuild();
    }
}

// ============================================================================
// Triangle (player ship)
// ============================================================================

export class Triangle extends Entity {
    constructor() {
        super();
        this.mass = Triangle.mass;
        this.radius = 16.0;

        this.boostProgress = 0.0;
        this.reloadProgress = 0.0;
        this.secondaryCharge = 0.0;
        this.secondaryProgress = 0.0;
        this.resBoostProgress = 0.0;
        this.resReloadProgress = 0.0;
        this.resSecondaryCharge = 0.0;
        this.resSecondaryProgress = 0.0;

        this.name = "unnamed";
        this.target = null;
    }

    control(cont) {
        const rotationRad = this.rotation * degToRad;
        const xMul = Math.cos(rotationRad);
        const yMul = Math.sin(rotationRad);
        let rotateSpeed = Triangle.rotateSpeed;
        this.boostProgress += State.delta;
        if (this.reloadProgress < Triangle.reload) this.reloadProgress += State.delta;
        if (this.secondaryProgress < Triangle.secondaryReload) this.secondaryProgress += State.delta;
        this.secondaryCharge = Math.min(Triangle.secondaryStockpile, this.secondaryCharge + Triangle.secondaryRegen * State.delta);

        if (cont.slowrotate) {
            rotateSpeed *= Triangle.slowRotateSpeed;
        }
        if (cont.forward) {
            this.addAccel(Triangle.accel * xMul, Triangle.accel * yMul);
        } else if (cont.backward) {
            this.addAccel(-Triangle.accel * xMul, -Triangle.accel * yMul);
        }
        if (cont.turnleft) {
            this.rotateVel -= rotateSpeed * State.delta;
        } else if (cont.turnright) {
            this.rotateVel += rotateSpeed * State.delta;
        }
        if (this.rotateVel > 0.0) {
            this.rotateVel = Math.max(0.0, this.rotateVel - rotateSpeed * State.delta * Triangle.rotateSlowSpeedMult);
        } else {
            this.rotateVel = Math.min(0.0, this.rotateVel + rotateSpeed * State.delta * Triangle.rotateSlowSpeedMult);
        }
        if (cont.boost && this.boostProgress > Triangle.boostCooldown) {
            this.addVelocity(Triangle.boostStrength * xMul, Triangle.boostStrength * yMul);
            this.boostProgress = 0.0;
        }
        if (cont.primaryfire && this.reloadProgress >= Triangle.reload) {
            if (State.authority) {
                const proj = new Missile();
                if (State.simulating) State.simCleanupBuffer.push(proj);
                proj.setPosition(this.x + (this.radius + proj.radius * 3.0) * xMul, this.y + (this.radius + proj.radius * 3.0) * yMul);
                this.addVelocity(-Triangle.shootPower * xMul * proj.mass / this.mass, -Triangle.shootPower * yMul * proj.mass / this.mass);
                proj.setVelocity(this.velX + Triangle.shootPower * xMul, this.velY + Triangle.shootPower * yMul);
                proj.rotation = this.rotation;
                proj.rotateVel = this.rotateVel;
                proj.owner = this;
                proj.target = this.target;
            }
            this.reloadProgress -= Triangle.reload;
        }
        if (cont.secondaryfire && this.secondaryCharge >= 1.0 && this.secondaryProgress >= Triangle.secondaryReload) {
            if (State.authority) {
                const proj = new Projectile();
                proj.setPosition(this.x + (this.radius + proj.radius * 3.0) * xMul, this.y + (this.radius + proj.radius * 3.0) * yMul);
                this.addVelocity(-Triangle.secondaryShootPower * xMul * proj.mass / this.mass, -Triangle.secondaryShootPower * yMul * proj.mass / this.mass);
                let shootX = xMul;
                let shootY = yMul;
                if (this.target) {
                    const dX = this.target.x - this.x;
                    const dY = this.target.y - this.y;
                    const headIn = Math.atan2(dY, dX);
                    const dVx = this.target.velX - this.velX;
                    const dVy = this.target.velY - this.velY;
                    const dVtg = dVy * Math.cos(headIn) - dVx * Math.sin(headIn);
                    const Vin = dVtg >= Triangle.secondaryShootPower ? 0.0 : Math.sqrt(Triangle.secondaryShootPower * Triangle.secondaryShootPower - dVtg * dVtg);
                    const targetHeading = Vin === 0.0 ? headIn : Math.atan2(dVtg, Vin) + headIn;
                    const angdiff = deltaAngleRad(rotationRad, targetHeading);
                    const ang = rotationRad + Math.sign(angdiff) * Math.min(Math.abs(angdiff), Triangle.maxSecondaryAngle * degToRad);
                    shootX = Math.cos(ang);
                    shootY = Math.sin(ang);
                }
                proj.setVelocity(this.velX + Triangle.secondaryShootPower * shootX, this.velY + Triangle.secondaryShootPower * shootY);
                proj.rotation = this.rotation;
                proj.rotateVel = this.rotateVel;
                if (State.simulating) State.simCleanupBuffer.push(proj);
            }
            this.secondaryCharge -= 1.0;
            this.secondaryProgress -= Triangle.secondaryReload;
            if (State.simulating && this.secondaryCharge < 1.0) {
                cont.secondaryfire = 0;
            }
        }
    }

    draw(ctx) {
        drawEntityTrajectory(ctx, this);
        const sx = this.x + State.drawShiftX;
        const sy = this.y + State.drawShiftY;
        drawPolygon(ctx, sx, sy, this.radius, 3, this.rotation * degToRad,
            `rgb(${this.color[0]},${this.color[1]},${this.color[2]})`, null, 0);
        }

        simSetup() {
            super.simSetup();
            this.resBoostProgress = this.boostProgress;
            this.resReloadProgress = this.reloadProgress;
            this.resSecondaryCharge = this.secondaryCharge;
            this.resSecondaryProgress = this.secondaryProgress;
        }
        simReset() {
            super.simReset();
            this.boostProgress = this.resBoostProgress;
            this.reloadProgress = this.resReloadProgress;
            this.secondaryCharge = this.resSecondaryCharge;
            this.secondaryProgress = this.resSecondaryProgress;
        }

        onEntityDelete(d) {
            super.onEntityDelete(d);
            if (this.target === d) this.target = null;
        }

        type() { return Entities.Triangle; }

        // --- Network serialization ---
        loadCreatePacket(w) {
            w.writeU8(this.type()).writeU32(this.id).writeDouble(this.x).writeDouble(this.y)
            .writeDouble(this.velX).writeDouble(this.velY).writeDouble(this.rotation).writeString(this.name);
        }
        unloadCreatePacket(r) {
            this.id = r.readU32();
            this.x = r.readDouble(); this.y = r.readDouble();
            this.velX = r.readDouble(); this.velY = r.readDouble();
            this.rotation = r.readDouble();
            this.name = r.readString();
        }
        loadSyncPacket(w) {
            w.writeU32(this.id).writeDouble(this.x).writeDouble(this.y)
            .writeDouble(this.velX).writeDouble(this.velY).writeDouble(this.rotation);
        }
        unloadSyncPacket(r) {
            this.syncX = r.readDouble(); this.syncY = r.readDouble();
            this.syncVelX = r.readDouble(); this.syncVelY = r.readDouble();
            this.rotation = r.readDouble();
        }
    }

    Triangle.mass = 1.0e7;
    Triangle.accel = 96.0;
    Triangle.rotateSlowSpeedMult = 2.0 / 3.0;
    Triangle.rotateSpeed = 180.0;
    Triangle.boostCooldown = 12.0;
    Triangle.boostStrength = 320.0;
    Triangle.reload = 8.0;
    Triangle.shootPower = 120.0;
    Triangle.secondaryRegen = 0.3;
    Triangle.secondaryReload = 1.0;
    Triangle.secondaryStockpile = 6.0;
    Triangle.secondaryShootPower = 25000.0;
    Triangle.maxSecondaryAngle = 9.0;
    Triangle.slowRotateSpeed = 0.02;

    // ============================================================================
    // CelestialBody (planet / star / black hole)
    // ============================================================================

    export class CelestialBody extends Entity {
        constructor(radiusOrGhost, mass) {
            super();
            this.star = false;
            this.blackhole = false;
            if (typeof radiusOrGhost === "number") {
                this.radius = radiusOrGhost;
                if (mass !== undefined) {
                    this.mass = mass;
                } else {
                    this.mass = 1.0e18;
                }
                this.gravitates = true;
            } else {
                // "ghost" constructor — removes self from updateGroup
                for (let i = 0; i < State.updateGroup.length; i++) {
                    if (State.updateGroup[i] === this) {
                        State.updateGroup[i] = State.updateGroup[State.updateGroup.length - 1];
                        State.updateGroup.pop();
                        break;
                    }
                }
            }
        }

        postMassUpdate() {
            if (this.mass > State.gen_chandrasekharLimit || this.blackhole) {
                this.setColor(0, 0, 0);
                this.blackhole = true;
                this.radius = 2.0 * State.G * this.mass / CC;
            } else if (this.mass > State.gen_starMass * State.gen_starMassReq) {
                if (!State.simulating) {
                    const colorFactor = Math.pow(State.gen_starMass / this.mass, State.gen_starColorFactor);
                    this.setColor(
                        Math.floor(255.0 * Math.max(0.0, Math.min(1.0, 2.0 - colorFactor))),
                        Math.floor(255.0 * Math.max(0.0, Math.min(1.0, 1.9 - colorFactor))),
                        Math.floor(255.0 * Math.max(0.0, Math.min(1.0, colorFactor - 0.72)))
                    );
                    this.star = true;
                }
                this.radius = State.gen_starRadius * Math.pow(this.mass / State.gen_starMass, 1.0 / State.gen_starDensityFactor);
            } else {
                this.radius = Math.pow(this.mass / State.gen_baseDensity, 1.0 / State.gen_densityFactor);
            }
        }

        collide(with_, specialOnly) {
            super.collide(with_, specialOnly);
            if (!with_.active) return;
            if (State.authority && this.star && with_.type() === Entities.Triangle) {
                // During prediction (simulating), don't respawn — just mark inactive.
                // This matches the C++ !simulating check. Without it, setupShip()
                // teleports the ship during prediction, corrupting the trajectory.
                if (!State.simulating) {
                    setupShip(with_);
                    pushMessage(`<${with_.name}> has been incinerated.`);
                } else {
                    with_.active = false;
                }
            } else if (State.authority && with_.type() === Entities.CelestialBody) {
                const other = with_;
                if ((this.mass >= other.mass && !other.blackhole) || this.blackhole) {
                    this.mass += other.mass;
                    this.postMassUpdate();
                    other.active = false;
                }
            }
        }

        draw(ctx) {
            drawEntityTrajectory(ctx, this);
            const sx = this.x + State.drawShiftX;
            const sy = this.y + State.drawShiftY;
            if (this.star && !this.blackhole) {
                ctx.fillStyle = `rgba(${this.color[0]},${this.color[1]},${this.color[2]},0.18)`;
                ctx.beginPath();
                ctx.arc(sx, sy, this.radius * 1.6, 0, TAU);
                ctx.fill();
            }
            drawPolygon(ctx, sx, sy, this.radius,
                Math.max(4, Math.floor(Math.sqrt(this.radius))),
                0,
                `rgb(${this.color[0]},${this.color[1]},${this.color[2]})`, null, 0);
                if (this.blackhole) {
                    ctx.strokeStyle = "rgba(255,255,255,0.55)";
                    ctx.lineWidth = Math.max(1, this.radius * 0.04);
                    ctx.beginPath();
                    ctx.arc(sx, sy, this.radius * 1.05, 0, TAU);
                    ctx.stroke();
                }
            }

            type() { return Entities.CelestialBody; }

            // --- Network serialization ---
            loadCreatePacket(w) {
                w.writeU8(this.type()).writeDouble(this.radius).writeU32(this.id)
                .writeDouble(this.x).writeDouble(this.y).writeDouble(this.velX).writeDouble(this.velY)
                .writeDouble(this.mass).writeBool(this.star).writeBool(this.blackhole)
                .writeU8(this.color[0]).writeU8(this.color[1]).writeU8(this.color[2]);
            }
            unloadCreatePacket(r) {
                this.id = r.readU32();
                this.x = r.readDouble(); this.y = r.readDouble();
                this.velX = r.readDouble(); this.velY = r.readDouble();
                this.mass = r.readDouble();
                this.star = r.readBool(); this.blackhole = r.readBool();
                this.color = [r.readU8(), r.readU8(), r.readU8()];
            }
            loadSyncPacket(w) {
                w.writeU32(this.id).writeDouble(this.x).writeDouble(this.y)
                .writeDouble(this.velX).writeDouble(this.velY);
            }
            unloadSyncPacket(r) {
                this.syncX = r.readDouble(); this.syncY = r.readDouble();
                this.syncVelX = r.readDouble(); this.syncVelY = r.readDouble();
            }
        }

        // ============================================================================
        // Projectile (railgun slug)
        // ============================================================================

        export class Projectile extends Entity {
            constructor() {
                super();
                this.radius = 4.0;
                this.mass = Projectile.mass;
                this.color = [180, 60, 60];
            }

            collide(with_, specialOnly) {
                if (with_.type() === Entities.Triangle) {
                    if (State.authority) {
                        // During prediction (simulating), don't respawn — just mark inactive.
                        if (!State.simulating) {
                            setupShip(with_);
                            pushMessage(`<${with_.name}> has been killed.`);
                            State.kills++;
                        } else {
                            with_.active = false;
                        }
                        this.active = false;
                    }
                } else if (with_.type() === Entities.CelestialBody) {
                    if (State.authority) this.active = false;
                } else if (with_.type() === Entities.Projectile || with_.type() === Entities.Missile) {
                    if (State.authority) {
                        this.active = false;
                        with_.active = false;
                    }
                } else {
                    super.collide(with_, specialOnly);
                }
            }

            draw(ctx) {
                drawEntityTrajectory(ctx, this);
                const sx = this.x + State.drawShiftX;
                const sy = this.y + State.drawShiftY;
                drawPolygon(ctx, sx, sy, this.radius, 6, this.rotation * degToRad,
                    `rgb(${this.color[0]},${this.color[1]},${this.color[2]})`, null, 0);
                }

                type() { return Entities.Projectile; }

                // --- Network serialization ---
                loadCreatePacket(w) {
                    w.writeU8(this.type()).writeU32(this.id)
                    .writeDouble(this.x).writeDouble(this.y).writeDouble(this.velX).writeDouble(this.velY);
                }
                unloadCreatePacket(r) {
                    this.id = r.readU32();
                    this.x = r.readDouble(); this.y = r.readDouble();
                    this.velX = r.readDouble(); this.velY = r.readDouble();
                }
                loadSyncPacket(w) {
                    w.writeU32(this.id).writeDouble(this.x).writeDouble(this.y)
                    .writeDouble(this.velX).writeDouble(this.velY);
                }
                unloadSyncPacket(r) {
                    this.syncX = r.readDouble(); this.syncY = r.readDouble();
                    this.syncVelX = r.readDouble(); this.syncVelY = r.readDouble();
                }
            }

            Projectile.mass = 5.0e3;

            // ============================================================================
            // Missile (guided, with fuel)
            // ============================================================================

            export class Missile extends Projectile {
                constructor() {
                    super();
                    this.radius = 4.0;
                    this.fuel = Missile.startingFuel;
                    this.mass = Missile.mass;
                    this.color = [180, 0, 0];

                    this.target = null;
                    this.owner = null;
                    this.resFuel = 0;
                    this.prevItime = 0;
                    this.thrust = true;
                }

                update2() {
                    if (this.target) {
                        const dVx = this.target.velX - this.velX;
                        const dVy = this.target.velY - this.velY;
                        const dX = this.target.x - this.x;
                        const dY = this.target.y - this.y;
                        const refRot = Math.atan2(dVy, dVx);
                        const vel = dVx / Math.cos(refRot);
                        const projX = dX * Math.cos(refRot) + dY * Math.sin(refRot);
                        const projY = dY * Math.cos(refRot) - dX * Math.sin(refRot);
                        const accel = Missile.accel * this.fuel / Missile.startingFuel;
                        let itimef = guessInterceptTime(0.0, -projX, -vel, projY, accel);
                        let itime = guessInterceptTime(itimef, -projX, -vel, projY, accelAt(itimef, this.fuel, accel));
                        for (let i = 0; i < Missile.guidanceIterations; ++i) {
                            itime = guessInterceptTime(itime, -projX, -vel, projY, accelAt(itime, this.fuel, accel));
                            itimef = guessInterceptTime(itimef, -projX, -vel, projY, accel);
                        }
                        const fullthrust = itimef < this.fuel * Missile.fullThrustThreshold;
                        const thrust = ((this.prevItime - itime) < Missile.leastItimeDecrease * State.delta || fullthrust) && this.fuel > 0.0;
                        const targetRot = Math.atan2(dY + dVy * itime, dX + dVx * itime);
                        const finangle = degToRad * (this.rotation + Math.abs(this.rotateVel) * this.rotateVel / (2.0 * Missile.rotateSpeed));
                        this.rotateVel += State.delta * (deltaAngleRad(finangle, targetRot) > 0.0 ? Missile.rotateSpeed : -Missile.rotateSpeed);
                        if (Math.abs(deltaAngleRad(targetRot, this.rotation * degToRad)) < Missile.maxThrustAngle && thrust) {
                            const actaccel = fullthrust ? Missile.accel : accel;
                            this.addAccel(actaccel * Math.cos(targetRot), actaccel * State.delta * Math.sin(targetRot));
                            this.fuel -= State.delta * (fullthrust ? 1.0 : this.fuel / Missile.startingFuel);
                        }
                        this.prevItime = itime;
                    }
                    super.update2();
                }

                simSetup() {
                    super.simSetup();
                    this.resFuel = this.fuel;
                }
                simReset() {
                    super.simReset();
                    this.fuel = this.resFuel;
                }

                onEntityDelete(d) {
                    super.onEntityDelete(d);
                    if (this.owner === d) this.owner = null;
                    if (this.target === d) {
                        this.target = (d instanceof Missile && d.owner !== this.owner) ? d.owner : null;
                    }
                }

                draw(ctx) {
                    drawEntityTrajectory(ctx, this);
                    const sx = this.x + State.drawShiftX;
                    const sy = this.y + State.drawShiftY;
                    drawPolygon(ctx, sx, sy, this.radius, 3, this.rotation * degToRad,
                        `rgb(${this.color[0]},${this.color[1]},${this.color[2]})`, null, 0);
                    }

                    type() { return Entities.Missile; }

                    // --- Network serialization ---
                    loadCreatePacket(w) {
                        const targetId = this.target ? this.target.id : 0xFFFFFFFF;
                        const ownerId = this.owner ? this.owner.id : 0xFFFFFFFF;
                        w.writeU8(this.type()).writeU32(this.id)
                        .writeDouble(this.x).writeDouble(this.y).writeDouble(this.velX).writeDouble(this.velY)
                        .writeDouble(this.rotation).writeU32(targetId).writeU32(ownerId);
                    }
                    unloadCreatePacket(r) {
                        this.id = r.readU32();
                        this.x = r.readDouble(); this.y = r.readDouble();
                        this.velX = r.readDouble(); this.velY = r.readDouble();
                        this.rotation = r.readDouble();
                        const entityId = r.readU32();
                        const ownerId = r.readU32();
                        this.target = entityId === 0xFFFFFFFF ? null : idLookup(entityId);
                        this.owner = ownerId === 0xFFFFFFFF ? null : idLookup(ownerId);
                    }
                    loadSyncPacket(w) {
                        w.writeU32(this.id).writeDouble(this.x).writeDouble(this.y)
                        .writeDouble(this.velX).writeDouble(this.velY).writeDouble(this.rotation).writeDouble(this.fuel);
                    }
                    unloadSyncPacket(r) {
                        this.syncX = r.readDouble(); this.syncY = r.readDouble();
                        this.syncVelX = r.readDouble(); this.syncVelY = r.readDouble();
                        this.rotation = r.readDouble();
                        this.fuel = r.readDouble();
                    }
                }

                Missile.mass = 1.0e3;
                Missile.accel = 196.0;
                Missile.rotateSpeed = 240.0;
                Missile.maxThrustAngle = 45.0 * degToRad;
                Missile.startingFuel = 80.0;
                Missile.leastItimeDecrease = 0.4;
                Missile.fullThrustThreshold = 0.95;
                Missile.guidanceIterations = 3;

                // ============================================================================
                // Helpers: trajectory drawing, polygon, intercept math
                // ============================================================================

                function guessInterceptTime(prev, x0, vel, y0, accel) {
                    const x = x0 + vel * prev;
                    const d = dst(x, y0);
                    const dd = vel * x / d;
                    const disc = dd * dd + 2.0 * accel * (d - dd * prev);
                    if (disc < 0 || accel === 0) return 0;
                    return (dd + Math.sqrt(disc)) / accel;
                }

                function accelAt(time, fuel, thrust) {
                    const fuel1 = fuel * Math.exp(-time / fuel);
                    const dV = thrust * (fuel - fuel1);
                    return dV / time;
                }

                export function drawTrajectory(ctx, color, traj) {
                    const to = traj.length;
                    if (!State.lastTrajectoryRef || to === 0) return;
                    const ref = State.lastTrajectoryRef;
                    ctx.lineWidth = 1.5 * g_camera.scale;
                    ctx.strokeStyle = `rgba(${Math.floor(color[0])},${Math.floor(color[1])},${Math.floor(color[2])},${State.trajectoryAlpha / 255})`;
                    ctx.beginPath();
                    let pwx = 0;
                    let pwy = 0;
                    for (let j = 0; j < to; j++) {
                        const p = traj[j];
                        const wx = ref.x + p.x + State.drawShiftX;
                        const wy = ref.y + p.y + State.drawShiftY;
                        if (j === 0) ctx.moveTo(wx, wy);
                        else {
                            const l = dst(wx - pwx, wy - pwy);
                            ctx.setLineDash([5 * l, 5 * l]);
                            ctx.lineTo(wx, wy);
                        }
                        pwx = wx;
                        pwy = wy;
                    }
                    ctx.stroke();
                }

                function drawEntityTrajectory(ctx, e) {
                    let color = [e.color[0], e.color[1], e.color[2]];
                    if (e instanceof CelestialBody && e.blackhole) color = [255, 255, 255];
                    drawTrajectory(ctx, color, e.trajectory);
                }

                // Draw an N-pointed regular polygon centered at (x,y) with rotation (radians).
                export function drawPolygon(ctx, x, y, radius, pointCount, rotation, fill, outline, outlineThickness) {
                    if (radius <= 0) return;
                    ctx.save();
                    ctx.translate(x, y);
                    ctx.rotate(rotation);
                    ctx.beginPath();
                    for (let i = 0; i < pointCount; i++) {
                        const a = (i * 2 * Math.PI) / pointCount;
                        const px = Math.cos(a) * radius;
                        const py = Math.sin(a) * radius;
                        if (i === 0) ctx.moveTo(px, py);
                        else ctx.lineTo(px, py);
                    }
                    ctx.closePath();
                    if (fill) {
                        ctx.fillStyle = fill;
                        ctx.fill();
                    }
                    if (outline && outlineThickness > 0) {
                        ctx.strokeStyle = outline;
                        ctx.lineWidth = outlineThickness;
                        ctx.stroke();
                    }
                    ctx.restore();
                }

                // ============================================================================
                // Game-flow helpers (ported from entities.cpp)
                // ============================================================================

                export function setupShip(ship) {
                    if (!ship) return;
                    const gravitators = [];
                    for (const e of State.updateGroup) {
                        if (e.type() === Entities.CelestialBody) gravitators.push(e);
                    }
                    if (gravitators.length === 0) {
                        pushMessage("No planets, couldn't spawn ship.");
                        return;
                    }
                    const planet = gravitators[Math.floor(rand_f(0, gravitators.length))];
                    const spawnDst = planet.radius * rand_f(State.shipSpawnDistanceMin, State.shipSpawnDistanceMax);
                    const spawnAngle = rand_f(-PI, PI);
                    ship.setPosition(planet.x + spawnDst * Math.cos(spawnAngle), planet.y + spawnDst * Math.sin(spawnAngle));
                    const vel = Math.sqrt(State.G * planet.mass / spawnDst);
                    ship.setVelocity(planet.velX + vel * Math.cos(spawnAngle + PI / 2.0), planet.velY + vel * Math.sin(spawnAngle + PI / 2.0));
                }

                function generateOrbitingPlanets(planets, amount, x, y, velx, vely, parentmass, minradius, maxradius, spawnDst) {
                    let totalMoons = 0;
                    const maxFactor = Math.sqrt(Math.pow(State.gen_minNextRadius * State.gen_maxNextRadius, amount * 0.5) * spawnDst);

                    for (let i = 0; i < amount; i++) {
                        spawnDst *= rand_f(State.gen_minNextRadius, State.gen_maxNextRadius);
                        const factor = Math.sqrt(spawnDst) / maxFactor;
                        const spawnAngle = rand_f(-PI, PI);
                        const radius = rand_f(minradius, maxradius * factor);
                        const mass = State.gen_baseDensity * Math.pow(radius, State.gen_densityFactor);
                        const star = mass > State.gen_starMass * State.gen_starMassReq;

                        const planet = new CelestialBody(star ? State.gen_starRadius * Math.pow(mass / State.gen_starMass, 1.0 / State.gen_starDensityFactor) : radius, mass);
                        planets.push(planet);
                        planet.postMassUpdate();

                        planet.setPosition(x + spawnDst * Math.cos(spawnAngle), y + spawnDst * Math.sin(spawnAngle));
                        const vel = Math.sqrt(State.G * parentmass / spawnDst);
                        planet.addVelocity(velx + vel * Math.cos(spawnAngle + PI / 2.0), vely + vel * Math.sin(spawnAngle + PI / 2.0));

                        if (!star) {
                            planet.setColor(Math.floor(rand_f(64, 255)), Math.floor(rand_f(64, 255)), Math.floor(rand_f(64, 255)));
                        }

                        const moons = Math.floor(rand_f(0, 1) * Math.pow(radius / State.gen_moonFactor, State.gen_moonPower));
                        const moonsDistance = planet.radius * (1.0 + rand_f(State.gen_minMoonDistance, State.gen_minMoonDistance + Math.pow(State.gen_maxMoonDistance, Math.min(1.0, 0.5 / (planet.radius / State.gen_maxPlanetRadius)))));
                        totalMoons += moons + generateOrbitingPlanets(planets, moons, planet.x, planet.y, planet.velX, planet.velY, planet.mass, State.gen_minMoonRadius, planet.radius * State.gen_maxMoonRadiusFrac, moonsDistance);
                    }
                    return totalMoons;
                }

                export function generateSystem() {
                    const planets = [];
                    const stars = [];
                    let starsN = 1;
                    while (rand_f(0, 1) < State.gen_extraStarChance) starsN += 1;
                    const angleSpacing = TAU / starsN;
                    let angle = 0.0;
                    const starsMass = State.gen_starMass * starsN;
                    const dist = (starsN - 1) * State.gen_starRadius * 2.0;
                    for (let i = 0; i < starsN; i++) {
                        const blackhole = rand_f(0, 1) < State.gen_blackholeChance;
                        const star = new CelestialBody(blackhole ? 2.0 * State.G * State.gen_starMass / CC : State.gen_starRadius, blackhole ? State.gen_starMass * 1.0001 : State.gen_starMass);
                        stars.push(star);
                        star.star = true;
                        star.blackhole = blackhole;
                        if (blackhole) star.setColor(0, 0, 0);
                        else star.setColor(255, 229, 97);

                        const posX = Math.cos(angle) * dist;
                        const posY = Math.sin(angle) * dist;
                        star.setPosition(posX, posY);
                        angle += angleSpacing;
                    }
                    if (starsN > 1) {
                        let aX = 0.0, aY = 0.0;
                        for (let i = 1; i < starsN; i++) {
                            const xdiff = stars[i].x - stars[0].x;
                            const ydiff = stars[i].y - stars[0].y;
                            const factor = stars[i].mass * State.G / Math.pow(xdiff * xdiff + ydiff * ydiff, 1.5);
                            aX += factor * xdiff;
                            aY += factor * ydiff;
                        }
                        const vel = Math.sqrt(dst(aX, aY) * dist);
                        angle = 0.0;
                        for (let i = 0; i < starsN; i++) {
                            stars[i].addVelocity(vel * Math.cos(angle + PI / 2.0), vel * Math.sin(angle + PI / 2.0));
                            angle += angleSpacing;
                        }
                    }
                    const spawnDst = State.gen_firstPlanetDistance * rand_f(State.gen_minNextRadius, State.gen_maxNextRadius) + dist + State.gen_starRadius;
                    const planetsN = Math.floor(rand_f(State.gen_baseMinPlanets, State.gen_baseMaxPlanets) * Math.sqrt(starsN));
                    const moonsN = generateOrbitingPlanets(planets, planetsN, 0.0, 0.0, 0.0, 0.0, starsMass, State.gen_minPlanetRadius, State.gen_maxPlanetRadius, spawnDst);
                    pushMessage(`Generated system: ${starsN} stars, ${planetsN} planets, ${moonsN} moons`);
                }

                export function fullClear(clearTriangles) {
                    State.worldBrightness = Math.floor(rand_f(State.worldBrightnessMin, State.worldBrightnessMax));
                    const triangles = [];
                    for (const e of State.updateGroup) {
                        if (clearTriangles || e.type() !== Entities.Triangle) {
                            // entity destructor
                        } else {
                            triangles.push(e);
                        }
                    }
                    if (clearTriangles) {
                        State.ownEntity = null;
                        State.updateGroup = [];
                    } else {
                        State.updateGroup = triangles;
                    }
                    State.trajectoryRef = null;
                    State.lastTrajectoryRef = null;
                }

                export function updateEntities() {
                    // Snapshot the array — C++ range-for doesn't iterate newly-added elements,
                    // but JS for...of does. Without this snapshot, missiles created during
                    // prediction would be updated in the same pass, causing NaN/crashes.
                    const group = State.updateGroup.slice();
                    for (const e of group) e.update1();
                    for (const e of group) e.update2();
                    for (const e of group) e.update3();
                }

                export function idLookup(id) {
                    let lo = 0, hi = State.updateGroup.length - 1;
                    while (lo <= hi) {
                        const mid = (lo + hi) >> 1;
                        if (State.updateGroup[mid].id === id) return State.updateGroup[mid];
                        if (State.updateGroup[mid].id < id) lo = mid + 1;
                        else hi = mid - 1;
                    }
                    return null;
                }

                // Initialize the synced-vars map. These must EXACTLY match the var names
                // in the C++ server's globals.hpp vars map (for synced=true vars).
                // If any are missing, the VarChange handler can't skip them (it doesn't
                // know their type/size) and all subsequent vars in the packet are lost.
                function initVars() {
                    const D = Types.Double, I = Types.Int32, B = Types.Bool, S = Types.String;
                    const v = State.vars;

                    // Prediction
                    v["predictDelta"] = { type: D, get: () => State.predictDelta, set: (x) => State.predictDelta = x };
                    v["predictSpacing"] = { type: D, get: () => State.predictSpacing, set: (x) => State.predictSpacing = x };
                    v["predictBaseScale"] = { type: D, get: () => State.predictBaseScale, set: (x) => State.predictBaseScale = x };
                    v["predictSteps"] = { type: I, get: () => State.predictSteps, set: (x) => State.predictSteps = x };

                    // Physics
                    v["gravityAccuracy"] = { type: D, get: () => State.gravityAccuracy, set: (x) => State.gravityAccuracy = x };
                    v["friction"] = { type: D, get: () => State.friction, set: (x) => State.friction = x };
                    v["collideRestitution"] = { type: D, get: () => State.collideRestitution, set: (x) => State.collideRestitution = x };
                    v["gravityStrength"] = { type: D, get: () => State.G, set: (x) => State.G = x };

                    // Simulation
                    v["deltaOverride"] = { type: D, get: () => State.deltaOverride, set: (x) => State.deltaOverride = x };
                    v["timescale"] = { type: D, get: () => State.timescale, set: (x) => State.timescale = x };

                    // System generation
                    v["gen_baseDensity"] = { type: D, get: () => State.gen_baseDensity, set: (x) => State.gen_baseDensity = x };
                    v["gen_densityFactor"] = { type: D, get: () => State.gen_densityFactor, set: (x) => State.gen_densityFactor = x };
                    v["gen_starDensityFactor"] = { type: D, get: () => State.gen_starDensityFactor, set: (x) => State.gen_starDensityFactor = x };
                    v["gen_baseMinPlanets"] = { type: I, get: () => State.gen_baseMinPlanets, set: (x) => State.gen_baseMinPlanets = x };
                    v["gen_baseMaxPlanets"] = { type: I, get: () => State.gen_baseMaxPlanets, set: (x) => State.gen_baseMaxPlanets = x };
                    v["gen_blackholeChance"] = { type: D, get: () => State.gen_blackholeChance, set: (x) => State.gen_blackholeChance = x };
                    v["gen_extraStarChance"] = { type: D, get: () => State.gen_extraStarChance, set: (x) => State.gen_extraStarChance = x };
                    v["gen_firstPlanetDistance"] = { type: D, get: () => State.gen_firstPlanetDistance, set: (x) => State.gen_firstPlanetDistance = x };
                    v["gen_minNextRadius"] = { type: D, get: () => State.gen_minNextRadius, set: (x) => State.gen_minNextRadius = x };
                    v["gen_maxNextRadius"] = { type: D, get: () => State.gen_maxNextRadius, set: (x) => State.gen_maxNextRadius = x };
                    v["gen_minPlanetRadius"] = { type: D, get: () => State.gen_minPlanetRadius, set: (x) => State.gen_minPlanetRadius = x };
                    v["gen_maxPlanetRadius"] = { type: D, get: () => State.gen_maxPlanetRadius, set: (x) => State.gen_maxPlanetRadius = x };
                    v["gen_minMoonDistance"] = { type: D, get: () => State.gen_minMoonDistance, set: (x) => State.gen_minMoonDistance = x };
                    v["gen_maxMoonDistance"] = { type: D, get: () => State.gen_maxMoonDistance, set: (x) => State.gen_maxMoonDistance = x };
                    v["gen_moonFactor"] = { type: D, get: () => State.gen_moonFactor, set: (x) => State.gen_moonFactor = x };
                    v["gen_moonPower"] = { type: D, get: () => State.gen_moonPower, set: (x) => State.gen_moonPower = x };
                    v["gen_starMass"] = { type: D, get: () => State.gen_starMass, set: (x) => State.gen_starMass = x };
                    v["gen_starRadius"] = { type: D, get: () => State.gen_starRadius, set: (x) => State.gen_starRadius = x };
                    v["gen_starMassReq"] = { type: D, get: () => State.gen_starMassReq, set: (x) => State.gen_starMassReq = x };
                    v["gen_starColorFactor"] = { type: D, get: () => State.gen_starColorFactor, set: (x) => State.gen_starColorFactor = x };
                    v["gen_chandrasekharLimit"] = { type: D, get: () => State.gen_chandrasekharLimit, set: (x) => State.gen_chandrasekharLimit = x };

                    // Projectile stats
                    v["projectile_mass"] = { type: D, get: () => Projectile.mass, set: (x) => Projectile.mass = x };

                    // Missile stats
                    v["missile_mass"] = { type: D, get: () => Missile.mass, set: (x) => Missile.mass = x };
                    v["missile_accel"] = { type: D, get: () => Missile.accel, set: (x) => Missile.accel = x };
                    v["missile_rotateSpeed"] = { type: D, get: () => Missile.rotateSpeed, set: (x) => Missile.rotateSpeed = x };
                    v["missile_maxThrustAngle"] = { type: D, get: () => Missile.maxThrustAngle, set: (x) => Missile.maxThrustAngle = x };
                    v["missile_leastItimeDecrease"] = { type: D, get: () => Missile.leastItimeDecrease, set: (x) => Missile.leastItimeDecrease = x };
                    v["missile_fullThrustThreshold"] = { type: D, get: () => Missile.fullThrustThreshold, set: (x) => Missile.fullThrustThreshold = x };
                    v["missile_startingFuel"] = { type: D, get: () => Missile.startingFuel, set: (x) => Missile.startingFuel = x };

                    // Triangle (ship) stats
                    v["triangle_mass"] = { type: D, get: () => Triangle.mass, set: (x) => Triangle.mass = x };
                    v["triangle_accel"] = { type: D, get: () => Triangle.accel, set: (x) => Triangle.accel = x };
                    v["triangle_rotateSlowSpeedMult"] = { type: D, get: () => Triangle.rotateSlowSpeedMult, set: (x) => Triangle.rotateSlowSpeedMult = x };
                    v["triangle_rotateSpeed"] = { type: D, get: () => Triangle.rotateSpeed, set: (x) => Triangle.rotateSpeed = x };
                    v["triangle_boostCooldown"] = { type: D, get: () => Triangle.boostCooldown, set: (x) => Triangle.boostCooldown = x };
                    v["triangle_boostStrength"] = { type: D, get: () => Triangle.boostStrength, set: (x) => Triangle.boostStrength = x };
                    v["triangle_reload"] = { type: D, get: () => Triangle.reload, set: (x) => Triangle.reload = x };
                    v["triangle_shootPower"] = { type: D, get: () => Triangle.shootPower, set: (x) => Triangle.shootPower = x };
                    v["triangle_secondaryShootPower"] = { type: D, get: () => Triangle.secondaryShootPower, set: (x) => Triangle.secondaryShootPower = x };
                    v["triangle_secondaryRegen"] = { type: D, get: () => Triangle.secondaryRegen, set: (x) => Triangle.secondaryRegen = x };
                    v["triangle_secondaryReload"] = { type: D, get: () => Triangle.secondaryReload, set: (x) => Triangle.secondaryReload = x };
                    v["triangle_maxSecondaryAngle"] = { type: D, get: () => Triangle.maxSecondaryAngle, set: (x) => Triangle.maxSecondaryAngle = x };
                    v["triangle_slowRotateSpeed"] = { type: D, get: () => Triangle.slowRotateSpeed, set: (x) => Triangle.slowRotateSpeed = x };
                }
                initVars();
