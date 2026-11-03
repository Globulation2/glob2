// ZIP32 with stored members, data descriptors and bounded streaming memory.
import { Readable } from 'node:stream';
const table = Uint32Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let i = 0; i < 8; i++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
export interface ZipEntry {
  name: string;
  stream: Readable;
}
export function zipStream(entries: AsyncIterable<ZipEntry>): Readable {
  return Readable.from(
    (async function* () {
      let offset = 0;
      const central: Buffer[] = [];
      for await (const entry of entries) {
        if (!/^[a-z0-9-]+\/a[123]\.opus$/.test(entry.name))
          throw new Error('Unsafe music archive member');
        const name = Buffer.from(entry.name);
        const header = Buffer.alloc(30);
        header.writeUInt32LE(0x04034b50);
        header.writeUInt16LE(20, 4);
        header.writeUInt16LE(8, 6);
        header.writeUInt16LE(name.length, 26);
        const start = offset;
        yield header;
        yield name;
        offset += header.length + name.length;
        let crc = 0xffffffff,
          size = 0;
        for await (const part of entry.stream) {
          const chunk = Buffer.from(part as Uint8Array);
          for (const byte of chunk) crc = (table[(crc ^ byte) & 255] ?? 0) ^ (crc >>> 8);
          size += chunk.length;
          offset += chunk.length;
          if (offset > 64 * 1024 * 1024) throw new Error('Music archive exceeds 64 MiB');
          yield chunk;
        }
        crc = (crc ^ 0xffffffff) >>> 0;
        const descriptor = Buffer.alloc(16);
        descriptor.writeUInt32LE(0x08074b50);
        descriptor.writeUInt32LE(crc, 4);
        descriptor.writeUInt32LE(size, 8);
        descriptor.writeUInt32LE(size, 12);
        yield descriptor;
        offset += descriptor.length;
        const record = Buffer.alloc(46);
        record.writeUInt32LE(0x02014b50);
        record.writeUInt16LE(20, 4);
        record.writeUInt16LE(20, 6);
        record.writeUInt16LE(8, 8);
        record.writeUInt32LE(crc, 16);
        record.writeUInt32LE(size, 20);
        record.writeUInt32LE(size, 24);
        record.writeUInt16LE(name.length, 28);
        record.writeUInt32LE(start, 42);
        central.push(Buffer.concat([record, name]));
      }
      const directory = Buffer.concat(central);
      yield directory;
      const end = Buffer.alloc(22);
      end.writeUInt32LE(0x06054b50);
      end.writeUInt16LE(central.length, 8);
      end.writeUInt16LE(central.length, 10);
      end.writeUInt32LE(directory.length, 12);
      end.writeUInt32LE(offset, 16);
      yield end;
    })(),
  );
}
