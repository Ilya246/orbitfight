// Client networking — ported from src/net.cpp.
// Connects to a C++ orbitfight server through a WebSocket-to-TCP proxy.

import {
    State, Triangle, CelestialBody, Projectile, Missile,
    idLookup, fullClear, pushMessage, g_camera,
} from "./engine.js";
import { Packets, Entities, Types } from "./types.js";
import { PacketReader, PacketWriter } from "./packet.js";

export class NetworkClient {
    constructor(opts) {
        this.opts = opts;
        this.ws = null;
        this.state = "disconnected";
        this.error = null;
        this.rxBuffer = new Uint8Array(0);
    }

    connect() {
        return new Promise((resolve, reject) => {
            this.state = "connecting";
            this.error = null;

            const url = this.opts.proxyUrl;

            try {
                this.ws = new WebSocket(url);
                this.ws.binaryType = "arraybuffer";
            } catch (e) {
                this.state = "error";
                this.error = "WebSocket not supported";
                reject(e);
                return;
            }

            const timeout = setTimeout(() => {
                if (this.state === "connecting") {
                    this.ws?.close();
                    this.state = "error";
                    this.error = "Connection timed out";
                    reject(new Error("Connection timed out (10s)"));
                }
            }, 10000);

            this.ws.onopen = () => {
                clearTimeout(timeout);
                this.state = "connected";
                this.onServerConnection();
                resolve();
            };

            this.ws.onmessage = (ev) => {
                if (ev.data instanceof ArrayBuffer) {
                    this.onRawData(new Uint8Array(ev.data));
                } else if (ev.data instanceof Blob) {
                    ev.data.arrayBuffer().then((ab) => this.onRawData(new Uint8Array(ab)));
                }
            };

            this.ws.onerror = () => {
                clearTimeout(timeout);
                this.state = "error";
                this.error = "WebSocket connection failed — check that the proxy URL is correct and reachable";
                reject(new Error(this.error));
            };

            this.ws.onclose = (ev) => {
                clearTimeout(timeout);
                if (this.state === "connected") {
                    pushMessage(`Connection to server closed${ev.reason ? ": " + ev.reason : ""}. Continuing simulation locally.`);
                }
                this.state = "disconnected";
                State.authority = true;
                State.serverSocket = null;
            };
        });
    }

    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.state = "disconnected";
        State.authority = true;
        State.serverSocket = null;
    }

    send(w) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(w.toPacketBytes());
        }
    }

    onServerConnection() {
        State.authority = false;
        const nick = new PacketWriter();
        nick.writeU16(Packets.Nickname).writeString(this.opts.name);
        this.send(nick);
        const resize = new PacketWriter();
        resize.writeU16(Packets.ResizeView).writeDouble(g_camera.w * g_camera.scale).writeDouble(g_camera.h * g_camera.scale);
        this.send(resize);
    }

    onRawData(data) {
        const newBuf = new Uint8Array(this.rxBuffer.length + data.length);
        newBuf.set(this.rxBuffer, 0);
        newBuf.set(data, this.rxBuffer.length);
        this.rxBuffer = newBuf;

        while (this.rxBuffer.length >= 4) {
            const view = new DataView(this.rxBuffer.buffer, this.rxBuffer.byteOffset);
            const packetLen = view.getUint32(0, false); // big-endian
            if (packetLen > 16 * 1024 * 1024) {
                const hex = Array.from(this.rxBuffer.slice(0, 32))
                .map((b) => b.toString(16).padStart(2, "0"))
                .join(" ");
                console.error(
                    "Packet too large:", packetLen,
                    "(first 32 bytes hex):", hex,
                    "— closing connection."
                );
                this.ws?.close();
                this.rxBuffer = new Uint8Array(0);
                return;
            }
            if (this.rxBuffer.length < 4 + packetLen) break;

            const payload = this.rxBuffer.subarray(4, 4 + packetLen);
            const reader = new PacketReader(payload);
            try {
                this.handlePacket(reader);
            } catch (e) {
                console.error("Error handling packet:", e);
            }

            const remaining = new Uint8Array(this.rxBuffer.length - 4 - packetLen);
            remaining.set(this.rxBuffer.subarray(4 + packetLen));
            this.rxBuffer = remaining;
        }
    }

    handlePacket(r) {
        const type = r.readU16();
        if (State.debug && type !== Packets.SyncEntity) {
            console.log("Got packet", type, "size", r.remaining + 2);
        }

        switch (type) {
            case Packets.Ping: {
                const reply = new PacketWriter();
                reply.writeU16(Packets.Ping);
                this.send(reply);
                break;
            }
            case Packets.CreateEntity: {
                const entityType = r.readU8();
                this.createEntityFromPacket(entityType, r);
                break;
            }
            case Packets.SyncEntity: {
                const entityID = r.readU32();
                const entity = idLookup(entityID);
                if (entity) {
                    entity.unloadSyncPacket(r);
                    entity.synced = true;
                } else {
                    console.warn("Server referred to invalid entity", entityID, "in SyncEntity");
                }
                break;
            }
            case Packets.SyncDone: {
                for (const e of State.updateGroup) {
                    if (!e.synced) continue;
                    e.x = e.syncX; e.y = e.syncY;
                    e.velX = e.syncVelX; e.velY = e.syncVelY;
                    e.synced = false;
                }
                break;
            }
            case Packets.AssignEntity: {
                const entityID = r.readU32();
                State.ownEntity = idLookup(entityID);
                if (State.ownEntity) {
                    pushMessage("Assigned own entity " + entityID);
                }
                break;
            }
            case Packets.DeleteEntity: {
                const entityID = r.readU32();
                const e = idLookup(entityID);
                if (e) e.active = false;
                break;
            }
            case Packets.ColorEntity: {
                const entityID = r.readU32();
                const e = idLookup(entityID);
                if (e) {
                    e.color = [r.readU8(), r.readU8(), r.readU8()];
                }
                break;
            }
            case Packets.Chat: {
                const message = r.readString();
                pushMessage(message);
                break;
            }
            case Packets.PingInfo: {
                State.lastPing = r.readDouble();
                break;
            }
            case Packets.Name: {
                const entityID = r.readU32();
                const e = idLookup(entityID);
                if (e instanceof Triangle) {
                    e.name = r.readString();
                }
                break;
            }
            case Packets.PlanetCollision: {
                const entityID = r.readU32();
                const e = idLookup(entityID);
                if (e instanceof CelestialBody) {
                    e.mass = r.readDouble();
                    e.postMassUpdate();
                }
                break;
            }
            case Packets.FullClear: {
                const brightness = r.readI32();
                State.worldBrightness = brightness;
                fullClear(false);
                break;
            }
            case Packets.VarChange: {
                while (!r.atEnd()) {
                    const varName = r.readString();
                    const v = State.vars[varName];
                    if (!v) {
                        console.warn("Unknown var from server:", varName);
                        break;
                    }
                    switch (v.type) {
                        case Types.String: v.set(r.readString()); break;
                        case Types.Double: v.set(r.readDouble()); break;
                        case Types.Int8: v.set(r.readU8()); break;
                        case Types.Int32: v.set(r.readI32()); break;
                        case Types.Bool: v.set(r.readBool()); break;
                    }
                }
                break;
            }
            default:
            console.warn("Unknown packet", type, "received");
            break;
        }
    }

    createEntityFromPacket(entityType, r) {
        switch (entityType) {
            case Entities.Triangle: {
                const e = new Triangle();
                e.unloadCreatePacket(r);
                break;
            }
            case Entities.CelestialBody: {
                const radius = r.readDouble();
                const e = new CelestialBody(radius);
                e.unloadCreatePacket(r);
                break;
            }
            case Entities.Missile: {
                const e = new Missile();
                e.unloadCreatePacket(r);
                break;
            }
            case Entities.Projectile: {
                const e = new Projectile();
                e.unloadCreatePacket(r);
                break;
            }
            default:
            console.warn("Received entity of unknown type", entityType);
            break;
        }
    }

    sendControls(controls) {
        const w = new PacketWriter();
        w.writeU16(Packets.Controls);
        const byte =
        (controls.forward ? 1 : 0) |
        (controls.backward ? 2 : 0) |
        (controls.turnright ? 4 : 0) |
        (controls.turnleft ? 8 : 0) |
        (controls.boost ? 16 : 0) |
        (controls.slowrotate ? 32 : 0) |
        (controls.primaryfire ? 64 : 0) |
        (controls.secondaryfire ? 128 : 0);
        w.writeU8(byte);
        this.send(w);
    }

    sendResizeView() {
        const w = new PacketWriter();
        w.writeU16(Packets.ResizeView);
        w.writeDouble(g_camera.w * g_camera.scale);
        w.writeDouble(g_camera.h * g_camera.scale);
        this.send(w);
    }

    sendSetTarget(targetId) {
        const w = new PacketWriter();
        w.writeU16(Packets.SetTarget);
        w.writeU32(targetId === null ? 0xFFFFFFFF : targetId);
        this.send(w);
    }

    sendChat(message) {
        if (message.length === 0 || message.length > State.messageLimit) return;
        const w = new PacketWriter();
        w.writeU16(Packets.Chat).writeString(message);
        this.send(w);
    }
}
