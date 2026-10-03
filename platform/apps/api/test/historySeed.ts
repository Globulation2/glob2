// A realistic match history for tests and the web app's browser smoke test:
// registered players, a guest and a banned account; ratings on two ladders
// with AI entities on two sim versions; verified queue matches with rating
// history, team timelines and a replay; room matches (public and link-only);
// a pending and a running match; a catalog map with a preview and a report.
// Rows are written directly: this is the state the worker leaves behind.
import { createHash, randomBytes } from 'node:crypto';
import { deflateSync } from 'node:zlib';
import { sql, type Kysely } from 'kysely';
import { putContent, type BlobStore } from '@glob2/core';
import type { Database } from '@glob2/db';
import { STANDARD_RULES, simVersionKey, type MatchSetup, type SimVersion } from '@glob2/protocol';
import { displayRating } from '@glob2/play';
import { SIM } from './support.ts';

// Not imported from playSupport.ts, which needs the Vitest runtime: the
// browser smoke test's server (apps/web/e2e) seeds with this module too.
const OTHER_SIM: SimVersion = { ...SIM, dataHash: 'cd'.repeat(32) };

async function serveSim(db: Db, sim: SimVersion): Promise<void> {
  await db
    .insertInto('engine_agents')
    .values({
      id: `seed-agent-${simVersionKey(sim).slice(0, 12)}`,
      sim_version: simVersionKey(sim),
      kinds: ['generate-map', 'validate-map', 'render-preview', 'verify-match'],
      build: 'seed',
    })
    .onConflict((oc) => oc.column('id').doNothing())
    .execute();
}

type Db = Kysely<Database>;

export interface SeededHistory {
  sim: SimVersion;
  accounts: Record<'kestrel' | 'mirelle' | 'ana' | 'bradley' | 'guest' | 'banned', string>;
  /** Verified ranked 1v1 matches, newest first. */
  rankedMatches: string[];
  /** The newest verified match, with a replay and order rejections. */
  featuredMatch: string;
  vsAiMatch: string;
  publicRoomMatch: string;
  linkRoomMatch: string;
  pendingMatch: string;
  runningMatch: string;
  mapId: string;
  mapHash: string;
  reportId: string;
  replaySha256: string;
  /** A web session secret for `bradley` (an administrator). */
  adminSession: string;
  /** A web session secret for `kestrel` (a plain user). */
  userSession: string;
}

const DAY = 86_400_000;

function sha(data: Uint8Array | string): string {
  return createHash('sha256').update(data).digest('hex');
}

/** A small PNG (truecolour) drawn by `pixel(x, y) → [r, g, b]`. */
export function png(
  width: number,
  height: number,
  pixel: (x: number, y: number) => [number, number, number],
): Buffer {
  const crcTable = Array.from({ length: 256 }, (_, n) => {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  });
  const crc = (buf: Buffer) => {
    let c = 0xffffffff;
    for (const byte of buf) c = crcTable[(c ^ byte) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type: string, data: Buffer) => {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const sum = Buffer.alloc(4);
    sum.writeUInt32BE(crc(body));
    return Buffer.concat([length, body, sum]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 2;
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 3 + 1)] = 0;
    for (let x = 0; x < width; x++) {
      const [r, g, b] = pixel(x, y);
      const at = y * (width * 3 + 1) + 1 + x * 3;
      raw[at] = r;
      raw[at + 1] = g;
      raw[at + 2] = b;
    }
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

/** A map-like preview: grass, water and sand bands with two colony spots. */
export function previewPng(seed: number, size = 64): Buffer {
  return png(size, size, (x, y) => {
    const v = Math.sin((x + seed) / 7) + Math.cos((y * 1.3 + seed) / 9) + Math.sin((x + y) / 11);
    if (v < -1.1) return [52, 92, 186];
    if (v < -0.8) return [214, 196, 112];
    if ((x - 14) ** 2 + (y - 14) ** 2 < 10 || (x - size + 15) ** 2 + (y - size + 15) ** 2 < 10) {
      return [226, 64, 52];
    }
    return v > 1.2 ? [38, 104, 44] : [70, 140, 58];
  });
}

/** 512-tick team samples for a game of `ticks`, growing at `pace`. */
export function timeline(ticks: number, pace: number, phase = 0) {
  const points = [];
  for (let tick = 0; tick <= ticks; tick += 512) {
    const t = tick / 512;
    const wobble = 1 + 0.12 * Math.sin(t / 3 + phase);
    points.push({
      tick,
      units: Math.round((6 + pace * 1.6 * t) * wobble),
      buildings: Math.round(1 + pace * 0.45 * t),
      prestige: Math.round(pace * 0.9 * t * t * 0.05),
      hp: Math.round(200 + pace * 40 * t),
      attack: Math.round(pace * 3 * t),
      defense: Math.round(pace * 2 * t),
    });
  }
  return points;
}

function setup(
  sim: SimVersion,
  map: MatchSetup['map'],
  seats: MatchSetup['seats'],
  teams: number,
  alliances?: number[],
): MatchSetup {
  return {
    schemaVersion: 1,
    simVersion: sim,
    seed: 1234567,
    map,
    teams: Array.from({ length: teams }, (_, team) => ({
      team,
      alliance: alliances?.[team] ?? team,
    })),
    seats,
    rules: STANDARD_RULES,
    experiments: [],
  };
}

async function account(
  db: Db,
  name: string,
  kind: 'guest' | 'registered' = 'registered',
  extra: {
    role?: 'user' | 'moderator' | 'admin';
    status?: 'active' | 'banned';
    ageDays?: number;
  } = {},
): Promise<string> {
  const row = await db
    .insertInto('accounts')
    .values({
      kind,
      display_name: name,
      role: extra.role ?? 'user',
      status: extra.status ?? 'active',
      created_at: new Date(Date.now() - (extra.ageDays ?? 60) * DAY),
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  return row.id;
}

async function entity(db: Db, accountId: string): Promise<string> {
  const row = await db
    .insertInto('rating_entities')
    .values({ kind: 'account', account_id: accountId })
    .returning('id')
    .executeTakeFirstOrThrow();
  return row.id;
}

async function aiEntity(db: Db, ai: string, sim: SimVersion): Promise<string> {
  const row = await db
    .insertInto('rating_entities')
    .values({ kind: 'ai', ai_id: ai, ai_sim_version: simVersionKey(sim) })
    .returning('id')
    .executeTakeFirstOrThrow();
  return row.id;
}

async function rate(
  db: Db,
  entityId: string,
  ladder: string,
  mu: number,
  sigma: number,
  games: number,
  wins: number,
) {
  await db
    .insertInto('ratings')
    .values({ entity_id: entityId, ladder, mu, sigma, games, wins })
    .execute();
}

async function webSession(db: Db, accountId: string): Promise<string> {
  const secret = randomBytes(32).toString('base64url');
  await db
    .insertInto('web_sessions')
    .values({
      account_id: accountId,
      token_hash: sha(secret),
      expires_at: new Date(Date.now() + 7 * DAY),
    })
    .execute();
  return secret;
}

export async function seedHistory(
  db: Db,
  blobs: BlobStore,
  options: { replayBytes?: Uint8Array } = {},
): Promise<SeededHistory> {
  const sim = SIM;
  await serveSim(db, sim);
  const accounts = {
    kestrel: await account(db, 'Kestrel'),
    mirelle: await account(db, 'Mirelle'),
    ana: await account(db, 'Ana_M', 'registered', { ageDays: 6 }),
    bradley: await account(db, 'Bradley', 'registered', { role: 'admin', ageDays: 120 }),
    guest: await account(db, 'Guest-4821', 'guest', { ageDays: 1 }),
    banned: await account(db, 'Spammer', 'registered', { status: 'banned' }),
  };
  const entities = {
    kestrel: await entity(db, accounts.kestrel),
    mirelle: await entity(db, accounts.mirelle),
    ana: await entity(db, accounts.ana),
    bradley: await entity(db, accounts.bradley),
    banned: await entity(db, accounts.banned),
  };
  const nicowar = await aiEntity(db, 'nicowar', sim);
  await rate(db, entities.kestrel, 'ranked-1v1', 29.4, 3.1, 41, 27);
  await rate(db, entities.mirelle, 'ranked-1v1', 27.2, 4.2, 23, 12);
  await rate(db, entities.ana, 'ranked-1v1', 26.1, 6.3, 4, 3);
  await rate(db, entities.bradley, 'ranked-1v1', 28.0, 3.6, 8, 6);
  await rate(db, entities.bradley, 'ranked-2v2', 26.5, 5.6, 5, 3);
  await rate(db, entities.banned, 'ranked-1v1', 40, 2, 80, 79);
  await rate(db, nicowar, 'ranked-1v1', 27.5, 3.2, 31, 15);
  await rate(db, await aiEntity(db, 'maxima', sim), 'ranked-1v1', 25.1, 4.8, 14, 5);
  await rate(db, await aiEntity(db, 'cortex', OTHER_SIM), 'ranked-1v1', 24.0, 3.9, 50, 22);

  // A public catalog map, played by the ranked matches.
  const mapBytes = Buffer.from(`seeded map ${randomBytes(8).toString('hex')}`);
  const mapBlob = await putContent(blobs, mapBytes);
  await db
    .insertInto('blobs')
    .values({
      sha256: mapBlob.sha256,
      size: mapBlob.size,
      content_type: 'application/x-glob2-map',
      storage_key: mapBlob.key,
      visibility: 'private',
      owner_account_id: accounts.mirelle,
    })
    .execute();
  const preview = await putContent(blobs, previewPng(3, 96));
  await db
    .insertInto('blobs')
    .values({
      sha256: preview.sha256,
      size: preview.size,
      content_type: 'image/png',
      storage_key: preview.key,
      visibility: 'private',
    })
    .execute();
  const map = await db
    .insertInto('maps')
    .values({
      owner_account_id: accounts.mirelle,
      title: 'Even Ground Classic',
      description: 'A mirrored 1 vs 1 map with fords in the middle.',
      visibility: 'public',
      play_count: 8,
      download_count: 21,
      like_count: 1,
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  const version = await db
    .insertInto('map_versions')
    .values({
      map_id: map.id,
      hash: mapBlob.sha256,
      size: mapBlob.size,
      width: 128,
      height: 128,
      team_count: 2,
      min_version_minor: sim.versionMinor,
      sim_version: simVersionKey(sim),
      validation: 'valid',
      preview_hash: preview.sha256,
      preview_status: 'ready',
      preview_width: 96,
      preview_height: 96,
      file_title: 'Even Ground Classic',
      uploader_account_id: accounts.mirelle,
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  await db
    .updateTable('maps')
    .set({ latest_version_id: version.id })
    .where('id', '=', map.id)
    .execute();
  await db
    .insertInto('map_likes')
    .values({ map_id: map.id, account_id: accounts.kestrel })
    .execute();
  const report = await db
    .insertInto('map_reports')
    .values({
      map_id: map.id,
      reporter_account_id: accounts.ana,
      reason: 'broken',
      details: 'The north colony starts without wheat.',
    })
    .returning('id')
    .executeTakeFirstOrThrow();

  const replayBytes =
    options.replayBytes ?? Buffer.from(`fake replay ${randomBytes(8).toString('hex')}`);
  const replay = await putContent(blobs, replayBytes);
  await db
    .insertInto('blobs')
    .values({
      sha256: replay.sha256,
      size: replay.size,
      content_type: 'application/x-glob2-replay',
      storage_key: replay.key,
      visibility: 'public',
    })
    .execute();
  const record = await putContent(
    blobs,
    Buffer.from(`fake record ${randomBytes(8).toString('hex')}`),
  );
  await db
    .insertInto('blobs')
    .values({
      sha256: record.sha256,
      size: record.size,
      content_type: 'application/vnd.glob2.match-record',
      storage_key: record.key,
      visibility: 'private',
    })
    .execute();

  // ------------------------------------------------------ ranked matches

  const opponents = [accounts.kestrel, accounts.mirelle];
  const names: Record<string, string> = {
    [accounts.kestrel]: 'Kestrel',
    [accounts.mirelle]: 'Mirelle',
    [accounts.bradley]: 'Bradley',
  };
  const entityOf: Record<string, string> = {
    [accounts.kestrel]: entities.kestrel,
    [accounts.mirelle]: entities.mirelle,
    [accounts.bradley]: entities.bradley,
  };
  const rankedMatches: string[] = [];
  let bradleyMu = 25;
  let bradleySigma = 25 / 3;
  for (let i = 0; i < 8; i++) {
    const opponent = opponents[i % 2]!;
    const won = i % 3 !== 1;
    const endedAt = new Date(Date.now() - (8 - i) * 2 * DAY - 3_600_000);
    const ticks = 18_000 + i * 2_100;
    const match = await db
      .insertInto('matches')
      .values({
        sim_version: simVersionKey(sim),
        origin: 'queue',
        queue_id: 'ranked-1v1',
        rated: true,
        status: 'ended',
        verification: 'verified',
        rating_status: 'applied',
        setup: JSON.stringify(
          setup(
            sim,
            { kind: 'catalog', hash: mapBlob.sha256, mapId: map.id },
            [
              { seat: 0, kind: 'human', team: 0, name: 'Bradley', accountId: accounts.bradley },
              { seat: 1, kind: 'human', team: 1, name: names[opponent]!, accountId: opponent },
            ],
            2,
          ),
        ),
        seed: 1000 + i,
        map_hash: mapBlob.sha256,
        end_reason: 'completed',
        final_tick: ticks,
        created_at: new Date(endedAt.getTime() - ticks * 40 - 60_000),
        started_at: new Date(endedAt.getTime() - ticks * 40),
        ended_at: endedAt,
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    const before = { mu: bradleyMu, sigma: bradleySigma };
    bradleyMu += won ? 1.3 : -2.6;
    bradleySigma = Math.max(3.6, bradleySigma - 0.6);
    const after = { mu: bradleyMu, sigma: bradleySigma };
    const opponentBefore = { mu: 28, sigma: 4 };
    const opponentAfter = { mu: 28 + (won ? -0.7 : 0.7), sigma: 3.9 };
    const seats = [
      { seat: 0, account: accounts.bradley, outcome: won ? 'won' : 'lost', before, after },
      {
        seat: 1,
        account: opponent,
        outcome: won ? 'lost' : 'won',
        before: opponentBefore,
        after: opponentAfter,
      },
    ] as const;
    for (const seat of seats) {
      await db
        .insertInto('match_participants')
        .values({
          match_id: match.id,
          seat: seat.seat,
          team: seat.seat,
          kind: 'human',
          account_id: seat.account,
          rating_entity_id: entityOf[seat.account]!,
          display_name: names[seat.account]!,
          outcome: seat.outcome,
          disconnects: seat.seat === 1 && i === 7 ? 1 : 0,
          rating_before: displayRating(seat.before),
          rating_after: displayRating(seat.after),
        })
        .execute();
      await db
        .insertInto('rating_history')
        .values({
          match_id: match.id,
          entity_id: entityOf[seat.account]!,
          ladder: 'ranked-1v1',
          result: seat.outcome,
          mu_before: seat.before.mu,
          sigma_before: seat.before.sigma,
          mu_after: seat.after.mu,
          sigma_after: seat.after.sigma,
          display_before: displayRating(seat.before),
          display_after: displayRating(seat.after),
          created_at: endedAt,
        })
        .execute();
      const winner = seat.outcome === 'won';
      await db
        .insertInto('match_team_stats')
        .values({
          match_id: match.id,
          team: seat.seat,
          outcome: seat.outcome,
          prestige: winner ? 180 + i * 5 : 40 + i * 3,
          eliminated_tick: winner ? null : ticks - 40,
          statistics: JSON.stringify({
            unitsProduced: winner ? 140 + i : 90 + i,
            buildingsBuilt: winner ? 42 : 25,
            unitsLost: winner ? 30 : 80,
          }),
          timeline: JSON.stringify(timeline(ticks, winner ? 1.2 + i * 0.05 : 0.9, seat.seat)),
        })
        .execute();
    }
    await db
      .insertInto('match_artifacts')
      .values([
        { match_id: match.id, kind: 'replay', blob_sha256: replay.sha256 },
        { match_id: match.id, kind: 'record', blob_sha256: record.sha256 },
      ])
      .execute();
    await db
      .insertInto('engine_jobs')
      .values({
        kind: 'verify-match',
        sim_version: simVersionKey(sim),
        payload: JSON.stringify({ matchId: match.id }),
        status: 'succeeded',
        // A complete verdict: the match page decodes it against VerifyVerdict.
        result: JSON.stringify({
          verdict: 'verified',
          outcome: {
            finalTick: 30_000,
            teams: [
              { team: 0, outcome: 'won', prestige: 0 },
              { team: 1, outcome: 'lost', prestige: 0 },
            ],
            resultHash: replay.sha256,
            replayHash: replay.sha256,
          },
          ...(i === 7
            ? {
                orderRejections: [
                  {
                    seat: 1,
                    rejected: 2,
                    stale: 1,
                    reasons: { foreign_unit: 2 },
                    firstRejectedTick: 812,
                  },
                ],
              }
            : { orderRejections: [] }),
        }),
        completed_at: endedAt,
      })
      .execute();
    rankedMatches.unshift(match.id);
  }
  await db
    .updateTable('ratings')
    .set({ mu: bradleyMu, sigma: bradleySigma })
    .where('entity_id', '=', entities.bradley)
    .where('ladder', '=', 'ranked-1v1')
    .execute();

  // ------------------------------------------- vs AI, rooms, pending, running

  const generated = (generatorId: string, seed: number): MatchSetup['map'] => ({
    kind: 'generated',
    generator: {
      generatorId,
      revision: 1,
      params: { width: 7, height: 7, teams: 2, workers: 4 },
      seed,
      candidates: 3,
      startingUnitLevel: 0,
    },
    hash: sha(`${generatorId}:${seed}`),
  });

  const simpleMatch = async (values: {
    origin: 'queue' | 'room';
    queueId?: string;
    roomId?: string;
    status: 'running' | 'ended';
    verification: 'pending' | 'verified' | 'not_applicable';
    map: MatchSetup['map'];
    seats: {
      kind: 'human' | 'ai';
      account?: string;
      ai?: string;
      name: string;
      outcome?: 'won' | 'lost';
    }[];
    daysAgo: number;
    ticks?: number;
  }) => {
    const endedAt = new Date(Date.now() - values.daysAgo * DAY);
    const match = await db
      .insertInto('matches')
      .values({
        sim_version: simVersionKey(sim),
        origin: values.origin,
        queue_id: values.queueId ?? null,
        room_id: values.roomId ?? null,
        rated: values.origin === 'queue',
        status: values.status,
        verification: values.verification,
        rating_status: values.origin === 'queue' ? 'pending' : 'not_rated',
        setup: JSON.stringify(
          setup(
            sim,
            values.map,
            values.seats.map((s, seat) =>
              s.kind === 'human'
                ? { seat, kind: 'human' as const, team: seat, name: s.name, accountId: s.account! }
                : { seat, kind: 'ai' as const, team: seat, name: s.name, ai: s.ai as 'nicowar' },
            ),
            values.seats.length,
          ),
        ),
        seed: 77,
        map_hash: values.map.hash,
        final_tick: values.status === 'ended' ? (values.ticks ?? 20_000) : null,
        end_reason: values.status === 'ended' ? 'completed' : null,
        started_at: new Date(endedAt.getTime() - (values.ticks ?? 20_000) * 40),
        ended_at: values.status === 'ended' ? endedAt : null,
        created_at: new Date(endedAt.getTime() - (values.ticks ?? 20_000) * 40 - 30_000),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    for (const [seat, s] of values.seats.entries()) {
      await db
        .insertInto('match_participants')
        .values({
          match_id: match.id,
          seat,
          team: seat,
          kind: s.kind,
          account_id: s.account ?? null,
          ai_id: s.ai ?? null,
          display_name: s.name,
          outcome: s.outcome ?? null,
        })
        .execute();
      if (values.verification === 'verified' && s.outcome) {
        await db
          .insertInto('match_team_stats')
          .values({
            match_id: match.id,
            team: seat,
            outcome: s.outcome,
            prestige: s.outcome === 'won' ? 120 : 30,
            statistics: '{}',
            timeline: JSON.stringify(
              timeline(values.ticks ?? 20_000, s.outcome === 'won' ? 1.1 : 0.8, seat),
            ),
          })
          .execute();
      }
    }
    return match.id;
  };

  const vsAiMatch = await simpleMatch({
    origin: 'queue',
    queueId: 'ranked-1v1',
    status: 'ended',
    verification: 'verified',
    map: generated('symmetric-arena', 5),
    seats: [
      { kind: 'human', account: accounts.kestrel, name: 'Kestrel', outcome: 'won' },
      { kind: 'ai', ai: 'nicowar', name: 'Nicowar', outcome: 'lost' },
    ],
    daysAgo: 3,
  });

  const room = async (visibility: 'public' | 'link', code: string) =>
    (
      await db
        .insertInto('rooms')
        .values({
          code,
          name: visibility === 'public' ? 'FFA on big maps' : 'Friends only',
          visibility,
          status: 'closed',
          host_account_id: accounts.ana,
          sim_version: simVersionKey(sim),
          settings: '{}',
        })
        .returning('id')
        .executeTakeFirstOrThrow()
    ).id;
  const publicRoomMatch = await simpleMatch({
    origin: 'room',
    roomId: await room('public', 'PUBLIC01'),
    status: 'ended',
    verification: 'verified',
    map: generated('marchland', 9),
    seats: [
      { kind: 'human', account: accounts.ana, name: 'Ana_M', outcome: 'won' },
      { kind: 'human', account: accounts.guest, name: 'Guest-4821', outcome: 'lost' },
    ],
    daysAgo: 1,
    ticks: 30_000,
  });
  const linkRoomMatch = await simpleMatch({
    origin: 'room',
    roomId: await room('link', 'LINKONLY1'),
    status: 'ended',
    verification: 'verified',
    map: generated('marchland', 10),
    seats: [
      { kind: 'human', account: accounts.ana, name: 'Ana_M', outcome: 'lost' },
      { kind: 'human', account: accounts.mirelle, name: 'Mirelle', outcome: 'won' },
    ],
    daysAgo: 0.5,
  });
  const pendingMatch = await simpleMatch({
    origin: 'queue',
    queueId: 'ranked-1v1',
    status: 'ended',
    verification: 'pending',
    map: generated('even-ground', 11),
    seats: [
      { kind: 'human', account: accounts.bradley, name: 'Bradley' },
      { kind: 'human', account: accounts.kestrel, name: 'Kestrel' },
    ],
    daysAgo: 0.1,
  });
  const runningMatch = await simpleMatch({
    origin: 'queue',
    queueId: 'ranked-1v1',
    status: 'running',
    verification: 'pending',
    map: generated('even-ground', 12),
    seats: [
      { kind: 'human', account: accounts.mirelle, name: 'Mirelle' },
      { kind: 'ai', ai: 'maxima', name: 'Maxima' },
    ],
    daysAgo: 0,
  });

  // The relay's connection report for the featured match's human seats (the
  // match page's Connection table; RelayNetworkSummary v1 seat entries).
  await db
    .updateTable('match_participants')
    .set((eb) => ({
      network: sql`jsonb_build_object(
        'rtt_us', jsonb_build_object('count', 120, 'p50', 38000 + 40000 * ${eb.ref('seat')}, 'p95', 91000 + 160000 * ${eb.ref('seat')}),
        'lag_ticks', jsonb_build_object('count', 120, 'p50', 4, 'p95', 9),
        'connection', jsonb_build_object('disconnects', ${eb.ref('seat')}, 'grace_used_ms', 4200 * ${eb.ref('seat')}),
        'orders', jsonb_build_object('sequenced', 412, 'deferred', 3))`,
    }))
    .where('match_id', '=', rankedMatches[0]!)
    .where('kind', '=', 'human')
    .execute();

  return {
    sim,
    accounts,
    rankedMatches,
    featuredMatch: rankedMatches[0]!,
    vsAiMatch,
    publicRoomMatch,
    linkRoomMatch,
    pendingMatch,
    runningMatch,
    mapId: map.id,
    mapHash: mapBlob.sha256,
    reportId: report.id,
    replaySha256: replay.sha256,
    adminSession: await webSession(db, accounts.bradley),
    userSession: await webSession(db, accounts.kestrel),
  };
}

/** The queues the seeded history uses, for instance config. */
export const SEEDED_QUEUES = [
  { id: 'ranked-1v1', name: '1 vs 1 ranked', mode: '1v1' as const, rated: true },
  { id: 'ranked-2v2', name: '2 vs 2 ranked', mode: '2v2' as const, rated: true },
];
