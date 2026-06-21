// SFML-compatible binary Packet implementation.
//
// SFML Packet wire format:
//   [4-byte uint32 BE length][payload bytes]
// Payload values:
//   uint8_t  — 1 byte raw
//   uint16_t — 2 bytes big-endian (SFML uses htons/ntohs)
//   uint32_t — 4 bytes big-endian (SFML uses htonl/ntohl)
//   int32_t  — 4 bytes big-endian (SFML uses htonl/ntohl)
//   double   — 8 bytes little-endian (SFML uses raw reinterpret_cast = native)
//   bool     — 1 byte (0 or 1)
//   string   — [4-byte uint32 BE length][UTF-8 bytes, no null terminator]

export class PacketReader {
    constructor(data) {
        this.data = data;
        this.view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        this.offset = 0;
    }

    get remaining() {
        return this.data.byteLength - this.offset;
    }

    atEnd() {
        return this.offset >= this.data.byteLength;
    }

    readU8() {
        const v = this.view.getUint8(this.offset);
        this.offset += 1;
        return v;
    }

    readU16() {
        // SFML uses ntohs() → big-endian on wire
        const v = this.view.getUint16(this.offset, false); // false = big-endian
        this.offset += 2;
        return v;
    }

    readBool() {
        return this.readU8() !== 0;
    }

    readI32() {
        // SFML uses ntohl() → big-endian on wire
        const v = this.view.getInt32(this.offset, false); // false = big-endian
        this.offset += 4;
        return v;
    }

    readU32() {
        // SFML uses ntohl() → big-endian on wire
        const v = this.view.getUint32(this.offset, false); // false = big-endian
        this.offset += 4;
        return v;
    }

    readDouble() {
        // SFML uses raw reinterpret_cast → native byte order (little-endian on x86)
        const v = this.view.getFloat64(this.offset, true); // true = little-endian
        this.offset += 8;
        return v;
    }

    readString() {
        const len = this.readU32(); // length is Uint32 → big-endian
        const bytes = this.data.subarray(this.offset, this.offset + len);
        this.offset += len;
        return new TextDecoder().decode(bytes);
    }
}

export class PacketWriter {
    constructor() {
        this.bytes = [];
    }

    writeU8(v) {
        this.bytes.push(v & 0xff);
        return this;
    }

    writeU16(v) {
        // SFML uses htons() → big-endian on wire
        this.bytes.push((v >> 8) & 0xff, v & 0xff);
        return this;
    }

    writeBool(v) {
        this.bytes.push(v ? 1 : 0);
        return this;
    }

    writeI32(v) {
        // SFML uses htonl() → big-endian on wire
        this.bytes.push((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
        return this;
    }

    writeU32(v) {
        // SFML uses htonl() → big-endian on wire
        this.bytes.push((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
        return this;
    }

    writeDouble(v) {
        // SFML uses raw reinterpret_cast → native byte order (little-endian on x86)
        const buf = new ArrayBuffer(8);
        new DataView(buf).setFloat64(0, v, true); // true = little-endian
        const u8 = new Uint8Array(buf);
        for (let i = 0; i < 8; i++) this.bytes.push(u8[i]);
        return this;
    }

    writeString(s) {
        const encoded = new TextEncoder().encode(s);
        this.writeU32(encoded.length); // length is Uint32 → big-endian
        for (let i = 0; i < encoded.length; i++) this.bytes.push(encoded[i]);
        return this;
    }

    /** Produce the final payload bytes (without the length prefix). */
    toBytes() {
        return new Uint8Array(this.bytes);
    }

    /**
    * Produce the full SFML Packet wire bytes: [4-byte BE length][payload].
    * The length prefix is big-endian (network byte order, matching SFML's htonl).
    */
    toPacketBytes() {
        const payload = this.toBytes();
        const out = new Uint8Array(4 + payload.length);
        const len = payload.length;
        // Big-endian length prefix (network byte order, matching SFML's htonl)
        out[0] = (len >> 24) & 0xff;
        out[1] = (len >> 16) & 0xff;
        out[2] = (len >> 8) & 0xff;
        out[3] = len & 0xff;
        out.set(payload, 4);
        return out;
    }
}
