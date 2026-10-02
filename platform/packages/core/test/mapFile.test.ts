import { gzipSync } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import { checkMapFile, readMapHeader, readableSize } from '../src/mapFile.ts';

/** A map header as MapHeader::saveFields writes it, plus a little body. */
function header(options: { name?: string; minor?: number; teams?: number; saved?: boolean } = {}) {
  const name = Buffer.from(options.name ?? 'Two Rivers');
  const bytes = Buffer.alloc(4 + name.length + 17 + 16);
  bytes.writeUInt32BE(name.length, 0);
  name.copy(bytes, 4);
  const at = 4 + name.length;
  bytes.writeInt32BE(0, at);
  bytes.writeInt32BE(options.minor ?? 125, at + 4);
  bytes.writeInt32BE(options.teams ?? 2, at + 8);
  bytes.writeUInt32BE(0, at + 12);
  bytes.writeUInt8(options.saved ? 1 : 0, at + 16);
  return bytes;
}

const MAP = { format: 'map' as const, newestVersionMinor: 125 };

describe('checkMapFile', () => {
  it('accepts a plain map and returns its bytes', () => {
    const bytes = header();
    const check = checkMapFile(bytes, MAP);
    expect(check).toMatchObject({ ok: true, compressed: false });
    if (check.ok) {
      expect(check.bytes.equals(bytes)).toBe(true);
      expect(check.header).toMatchObject({ name: 'Two Rivers', versionMinor: 125, teamCount: 2 });
    }
  });

  it('unpacks the game’s .map.gz files so the stored bytes are what the game loads', () => {
    const bytes = header({ name: 'SmallForTwo' });
    const check = checkMapFile(gzipSync(bytes), MAP);
    expect(check).toMatchObject({ ok: true, compressed: true });
    if (check.ok) expect(check.bytes.equals(bytes)).toBe(true);
  });

  it('explains files that are not maps in plain words', () => {
    expect(checkMapFile(Buffer.from('a holiday photo'), MAP)).toEqual({
      ok: false,
      problem: 'not_a_map',
      message:
        "This file isn't a Globulation 2 map. Upload a .map or .map.gz file saved by the game or its map editor.",
    });
    expect(checkMapFile(Buffer.alloc(0), MAP)).toMatchObject({ ok: false, problem: 'empty' });
    expect(checkMapFile(Buffer.from([0x1f, 0x8b, 8, 0, 1, 2]), MAP)).toMatchObject({
      ok: false,
      problem: 'corrupt_gzip',
      message: expect.stringMatching(/damaged/),
    });
  });

  it('names newer versions, saves and size limits', () => {
    expect(checkMapFile(header({ minor: 126 }), MAP)).toMatchObject({
      ok: false,
      problem: 'newer_version',
      message: expect.stringMatching(/made with a newer version of Globulation 2/),
    });
    expect(checkMapFile(header({ saved: true }), MAP)).toMatchObject({
      ok: false,
      problem: 'save_not_map',
    });
    expect(checkMapFile(header(), { ...MAP, format: 'save' })).toMatchObject({
      ok: false,
      problem: 'map_not_save',
    });
    expect(checkMapFile(header({ saved: true }), { ...MAP, format: 'save' }).ok).toBe(true);
    expect(checkMapFile(header(), { ...MAP, maxBytes: 20 })).toMatchObject({
      ok: false,
      problem: 'too_large',
      message: 'This map is too big: the limit is 20 bytes.',
    });
    expect(checkMapFile(header({ minor: 57 }), MAP)).toMatchObject({
      ok: false,
      problem: 'older_version',
      message: expect.stringMatching(/very old version/),
    });
    // A gzip bomb stops at the limit.
    expect(
      checkMapFile(gzipSync(Buffer.alloc(5 * 1024 * 1024)), { ...MAP, maxBytes: 1024 * 1024 }),
    ).toMatchObject({
      ok: false,
      problem: 'too_large',
      message: 'This map is too big: unpacked, it is over the 1 MB limit.',
    });
  });

  it('reads headers and formats sizes', () => {
    expect(readMapHeader(Buffer.from('xx'))).toBeUndefined();
    expect(readMapHeader(header({ teams: 4 }))?.teamCount).toBe(4);
    expect(readableSize(16 * 1024 * 1024)).toBe('16 MB');
    expect(readableSize(1536 * 1024)).toBe('1.5 MB');
    expect(readableSize(300 * 1024)).toBe('300 KB');
  });
});
