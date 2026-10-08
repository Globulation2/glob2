import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { describe, it, expect } from 'vitest';
import {
  simVersionKey,
  type SimVersion,
  type GeneratorInfo,
  type GeneratorUpload,
  type ScriptGeneratorDescriptor,
  type RoomState as Room,
} from '@glob2/protocol';
import { putContent } from '@glob2/core';
import { handleEngineJobResult, insertBlob } from '@glob2/play';
import { createGeneratorExecutor } from '../../engine-agent/src/generatorValidation.ts';
import { DEFAULT_LIMITS } from '../../engine-agent/src/engine.ts';
import { REAL_GENERATOR_PACKAGE } from '../../engine-agent/test/generatorFixture.ts';
import { createHarness, RealtimeClient } from './support.ts';
import { registeredPlayer, serveSim, roomState, registerRelay, RELAY_KEY } from './playSupport.ts';
const root = resolve(import.meta.dirname, '../../../..');
const binary = process.env.GLOB2_BINARY,
  scratch = process.env.GLOB2_GENERATOR_SCRATCH;
describe.runIf(binary && scratch)('real shared generator publication and room flow', () => {
  it('publishes an isolated validated package and starts a pinned room whose guest downloads only the map', async () => {
    const { stdout } = await promisify(execFile)(resolve(root, binary!), ['--sim-version'], {
      cwd: root,
    });
    const sim = JSON.parse(stdout) as SimVersion;
    const executor = await createGeneratorExecutor(
      {
        binary: resolve(root, binary!),
        workdir: process.env.GLOB2_GENERATOR_WORKDIR ?? root,
        scratchRoot: scratch!,
        limits: DEFAULT_LIMITS,
        maxOutputBytes: 64 * 1024 * 1024,
      },
      sim,
    );
    const h = await createHarness();
    const people: RealtimeClient[] = [];
    try {
      await serveSim(h.database.db, sim);
      await h.database.db
        .updateTable('engine_agents')
        .set({ kinds: ['validate-generator', 'generate-script-map'] })
        .execute();
      const api = await h.start({
        instance: { auth: { providers: [], local: { enabled: true } } },
        relayKeys: [{ key: RELAY_KEY }],
      });
      const host = await registeredPlayer(api, 'RealGeneratorAuthor'),
        guest = await registeredPlayer(api, 'RealGeneratorGuest');
      for (const person of [host, guest]) {
        person.client.close();
        person.client = await RealtimeClient.connect(api.url);
        people.push(person.client);
        await person.client.hello(person.accessToken, sim, true);
      }
      const request = async (method: string, path: string, body?: unknown) =>
        fetch(api.url + path, {
          method,
          headers: {
            authorization: 'Bearer ' + host.accessToken,
            ...(body === undefined
              ? {}
              : {
                  'content-type':
                    body instanceof Uint8Array ? 'application/octet-stream' : 'application/json',
                }),
          },
          ...(body === undefined
            ? {}
            : { body: body instanceof Uint8Array ? Buffer.from(body) : JSON.stringify(body) }),
        });
      const bytes = REAL_GENERATOR_PACKAGE;
      const example = {
        seed: 19,
        params: { width: 7, height: 7, teams: 2, workers: 4 },
        candidates: 1,
        startingUnitLevel: 0 as const,
      };
      const staged = await request(
        'POST',
        '/api/v1/generator-uploads?example=' + encodeURIComponent(JSON.stringify(example)),
        bytes,
      );
      expect(staged.status, await staged.clone().text()).toBe(201);
      const upload = (await staged.json()) as GeneratorUpload;
      const job = await h.database.db
        .selectFrom('generator_validations')
        .select('job_id')
        .where('hash', '=', upload.sourceHash)
        .executeTakeFirstOrThrow();
      const validated = await executor.validate(bytes, example, new AbortController().signal);
      expect(validated.report.valid, validated.report.error).toBe(true);
      for (const [content, type] of [
        [validated.canonical!, 'application/x-glob2-generator'],
        [validated.png!, 'image/png'],
      ] as const) {
        const blob = await putContent(h.blobs, content);
        await insertBlob(h.database.db, blob.sha256, blob.size, type, 'private');
        if (type === 'image/png') validated.report.previewHash = blob.sha256;
      }
      await handleEngineJobResult(h.database.db, {
        jobId: job.job_id!,
        kind: 'validate-generator',
        agent: 'real-isolated',
        ok: true,
        result: validated.report,
      });
      const published = await request('POST', '/api/v1/generators', {
        uploadId: upload.id,
        name: 'Shared landscape',
        description: 'Isolated end-to-end fixture',
        visibility: 'private',
        version: '1',
        notes: '',
      });
      expect(published.status, await published.clone().text()).toBe(200);
      const g = (await published.json()) as GeneratorInfo;
      const exact = await request(
        'GET',
        `/api/v1/generators/${g.id}/versions/${g.latestVersion.id}/file`,
      );
      expect(Buffer.from(await exact.arrayBuffer())).toEqual(Buffer.from(validated.canonical!));
      const descriptor: ScriptGeneratorDescriptor = {
        ...example,
        libraryId: g.id,
        versionId: g.latestVersion.id,
        fileHash: g.latestVersion.hash,
        packageHash: g.latestVersion.packageHash,
        generatorId: g.latestVersion.metadata.id,
        revision: g.latestVersion.metadata.revision,
      };
      let room = (
        await host.client.ok('room.create', {
          name: 'Real generator room',
          visibility: 'link',
          map: { kind: 'scripted', generator: descriptor },
        })
      ).room as Room;
      room = (await guest.client.ok('room.join', { code: room.code })).room as Room;
      const generation = await h.database.db
        .selectFrom('engine_jobs')
        .selectAll()
        .where('kind', '=', 'generate-script-map')
        .where('status', '=', 'queued')
        .executeTakeFirstOrThrow();
      const world = await executor.generate(
          validated.canonical!,
          descriptor,
          new AbortController().signal,
        ),
        blob = await putContent(h.blobs, world.bytes);
      const { bytes: _bytes, ...facts } = world;
      void _bytes;
      await handleEngineJobResult(h.database.db, {
        jobId: generation.id,
        kind: 'generate-script-map',
        agent: 'real-isolated',
        ok: true,
        result: { ...facts, mapHash: blob.sha256, size: blob.size },
      });
      room = (await roomState(
        host.client,
        (r) => r.id === room.id && r.mapStatus === 'ready',
      )) as Room;
      const guestMap = await fetch(api.url + '/api/v1/blobs/maps/' + blob.sha256, {
        headers: { authorization: 'Bearer ' + guest.accessToken },
      });
      expect(guestMap.status).toBe(200);
      expect(Buffer.from(await guestMap.arrayBuffer())).toEqual(world.bytes);
      expect(
        (
          await fetch(api.url + `/api/v1/generators/${g.id}/versions/${g.latestVersion.id}/file`, {
            headers: { authorization: 'Bearer ' + guest.accessToken },
          })
        ).status,
      ).toBe(404);
      await guest.client.ok('room.setReady', { roomId: room.id, ready: true });
      await registerRelay(api, 'generator-real-relay');
      const started = await host.client.ok('room.start', { roomId: room.id });
      const match = await h.database.db
        .selectFrom('matches')
        .select(['setup', 'map_hash', 'sim_version'])
        .where('id', '=', started.matchId as string)
        .executeTakeFirstOrThrow();
      expect(match.sim_version).toBe(simVersionKey(sim));
      expect(match.map_hash).toBe(blob.sha256);
      expect(match.setup).toMatchObject({
        map: {
          kind: 'scripted',
          generator: descriptor,
          chosenSeed: world.chosenSeed,
          hash: blob.sha256,
        },
      });
      const legacy = await RealtimeClient.connect(api.url);
      people.push(legacy);
      const denied = await legacy.call('session.hello', {
        protocol: 1,
        accessToken: guest.accessToken,
        client: { platform: 'desktop', version: 'old', simVersion: sim, generatorSharing: false },
      });
      expect(denied.error?.code).toBe('update_required');
      expect(legacy.frames.some((f) => f.event === 'match.start')).toBe(false);
      const output = resolve(root, 'artifacts/generator-library/real-flow');
      await mkdir(output, { recursive: true });
      await writeFile(resolve(output, 'setup.json'), JSON.stringify(match.setup, null, 2));
      await writeFile(resolve(output, 'map.map'), world.bytes);
    } finally {
      for (const client of people) client.close();
      await h.close();
    }
  }, 180000);
});
