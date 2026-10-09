import sharp from 'sharp';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import {
  type TerrainStudio,
  validatePlan,
  assemble,
  plannerPrompt,
  entryKey,
  type RequestRow,
  type EntryArtwork,
} from '@glob2/terrain-studio';
import {
  SET_PACKAGE_MAX_BYTES,
  type TerrainStudioConfig,
  type ValidateSetResult,
  type SetPackage,
} from '@glob2/protocol';
import type { AgentBlobs } from '@glob2/engine/blobs';
import { Attempts, ProviderUncertain, ProviderBudget, type TerrainProvider } from './provider.ts';
import { imagePrompt, processArtwork } from './artwork.ts';
export interface Validator {
  validateSet(
    bytes: Uint8Array,
    signal?: AbortSignal,
    gallery?: boolean,
  ): Promise<{ report: ValidateSetResult; png?: Uint8Array }>;
}
export class Pipeline {
  readonly studio: TerrainStudio;
  readonly blobs: AgentBlobs;
  readonly provider: TerrainProvider;
  readonly engine: Validator;
  readonly config: TerrainStudioConfig;
  readonly root: string;
  readonly python: string;
  readonly simVersion: string;
  constructor(
    studio: TerrainStudio,
    blobs: AgentBlobs,
    provider: TerrainProvider,
    engine: Validator,
    config: TerrainStudioConfig,
    root: string,
    python: string,
    simVersion: string,
  ) {
    this.studio = studio;
    this.blobs = blobs;
    this.provider = provider;
    this.engine = engine;
    this.config = config;
    this.root = root;
    this.python = python;
    this.simVersion = simVersion;
  }

  async tick(signal: AbortSignal = new AbortController().signal) {
    await this.studio.recoverUncertain();
    const row = await this.studio.claim();
    if (!row) return false;
    const abort = new AbortController(),
      combined = AbortSignal.any([
        signal,
        abort.signal,
        AbortSignal.timeout((this.config.timeoutSeconds ?? 1800) * 1000),
      ]);
    const heartbeat = setInterval(() => {
      void this.studio.heartbeat(row).then(
        (alive) => {
          if (!alive) abort.abort();
        },
        () => abort.abort(),
      );
    }, 30000);
    try {
      await this.run(row, combined);
    } catch (error) {
      if (error instanceof ProviderBudget)
        await this.studio.checkpoint(row, 'processing', { serviceLimit: true });
      else if (error instanceof ProviderUncertain) {
        if ((await this.studio.request(row.id))?.status !== 'uncertain')
          await this.studio.checkpoint(row, 'uncertain', {});
      } else if (!abort.signal.aborted)
        await this.studio.finish(
          row,
          undefined,
          error instanceof Error ? error.message.slice(0, 1900) : 'Generation failed.',
        );
    } finally {
      clearInterval(heartbeat);
    }
    return true;
  }
  private async run(row: RequestRow, signal: AbortSignal) {
    const cfg = row.input.config ?? this.config;
    const textModel = cfg.textModel,
      imageModel = cfg.imageModel,
      providerCallsPerDay = cfg.providerCallsPerDay;
    if (!textModel || !imageModel || !providerCallsPerDay)
      throw Error('Terrain worker configuration is incomplete.');
    const attempts = new Attempts(
      this.studio,
      Math.min(providerCallsPerDay, this.config.providerCallsPerDay ?? providerCallsPerDay),
    );
    await this.studio.stage(row, 'prepare', 'running');
    const planned = await attempts.run(
      row,
      'design',
      textModel,
      { brief: row.input.brief, submission: row.input.submission },
      () =>
        this.provider.text(
          textModel,
          plannerPrompt(row.input.base, row.input.messages, row.input.brief),
          cfg.maxOutputTokens ?? 16000,
          signal,
        ),
    );
    let plan = validatePlan(JSON.parse(planned.text), row.input.base);
    if (plan.action === 'discuss') {
      await this.studio.stage(row, 'prepare', 'complete');
      await this.studio.finish(row, { text: plan.text, brief: plan.brief });
      return;
    }
    await this.studio.reserveBuild(row);
    await this.studio.stage(row, 'prepare', 'complete', plan.text);
    const references: Uint8Array[] = [];
    for (const hash of row.input.submission.references)
      references.push(await this.blobs.read(hash, 8 * 1024 * 1024));
    // Installed originals establish the default game style without copying them into a pack.
    if (!references.length)
      for (const stem of ['terrain0', 'terrain128', 'ressource0']) {
        try {
          let image: Buffer;
          try {
            image = await readFile(join(this.root, 'data/gfx', stem + '.png'));
          } catch {
            image = await readFile(join(this.root, 'data/gfx', stem + '.webp'));
          }
          references.push(await sharp(image).png().toBuffer());
        } catch {
          /* assets may be distributed as sheets */
        }
      }
    const art: Record<string, EntryArtwork> = {};
    let pack: SetPackage | undefined;
    for (let repair = 0; repair <= 2; repair++) {
      let error: string | undefined;
      try {
        await this.studio.stage(
          row,
          'artwork',
          'running',
          repair ? 'Repairing requested entries' : 'Creating selected entries',
        );
        for (const [index, entry] of plan.entries.entries()) {
          if (entry.operation === 'remove' || !entry.regenerateArt) continue;
          signal.throwIfAborted();
          const key = entryKey(row.input.base, entry.key);
          await this.studio.text(
            row,
            `${entry.name}: artwork ${index + 1}/${plan.entries.length}`,
            repair + 1,
          );
          const oldEntry = (
            entry.kind === 'terrain' ? row.input.base.terrains : row.input.base.resources
          ).find((e) => e['key'] === key);
          const sprite =
            entry.kind === 'terrain'
              ? row.input.base.assets.terrains[key]?.['sprite']
              : (oldEntry?.['presentation'] as { sprite?: string } | undefined)?.sprite;
          const sheet = row.input.base.assets.sheets.find((s) => sprite === 'data/sets/' + s.hash);
          const imageReferences = sheet
            ? [Buffer.from(sheet.png, 'base64url'), ...references].slice(0, 4)
            : references;
          const original = await attempts.run(
            row,
            `art:${key}:${createHash('sha256').update(entry.artPrompt).digest('hex')}`,
            imageModel,
            {
              prompt: entry.artPrompt,
              kind: entry.kind,
              references: row.input.submission.references,
            },
            async () => {
              const result = await this.provider.image(
                imageModel,
                imagePrompt(entry.kind, entry.artPrompt),
                imageReferences,
                entry.kind === 'resource',
                signal,
              );
              const hash = await this.blobs.write(result.bytes, 'image/png');
              return { hash, usage: result.usage, responseId: result.responseId };
            },
          );
          await this.studio.artifact(row, {
            stage: 'artwork',
            kind: 'source',
            label: entry.name + ' source',
            hash: original.hash,
          });
          const source = await this.blobs.read(original.hash, 16 * 1024 * 1024);
          const processed: EntryArtwork = await processArtwork(
            source,
            entry.kind,
            entry.animationFrames,
            this.root,
            this.python,
            signal,
          );
          art[key] = processed;
          if (entry.decorPrompt) {
            const decor = await attempts.run(
              row,
              `decor:${key}:${createHash('sha256').update(entry.decorPrompt).digest('hex')}`,
              imageModel,
              { prompt: entry.decorPrompt },
              async () => {
                const result = await this.provider.image(
                  imageModel,
                  imagePrompt('decor', entry.decorPrompt),
                  [source, ...references].slice(0, 4),
                  true,
                  signal,
                );
                return {
                  hash: await this.blobs.write(result.bytes, 'image/png'),
                  usage: result.usage,
                };
              },
            );
            await this.studio.artifact(row, {
              stage: 'artwork',
              kind: 'source',
              label: entry.name + ' decor source',
              hash: decor.hash,
            });
            processed.decor = (
              await processArtwork(
                await this.blobs.read(decor.hash, 16 * 1024 * 1024),
                'decor',
                1,
                this.root,
                this.python,
                signal,
              )
            ).sheet;
          }
        }
        await this.studio.stage(row, 'artwork', 'complete');
        await this.studio.stage(row, 'assemble', 'running');
        pack = assemble(row.input.base, plan, art);
        await this.studio.stage(row, 'assemble', 'complete');
        await this.studio.stage(row, 'checks', 'running');
        const bytes = Buffer.from(JSON.stringify(pack));
        if (bytes.length > SET_PACKAGE_MAX_BYTES) throw Error('Package too large.');
        const validation = await this.engine.validateSet(bytes, signal, true);
        if (validation.report.hash !== createHash('sha256').update(bytes).digest('hex'))
          throw Error('Validator returned the wrong package hash.');
        await this.studio.check(row, {
          id: 'engine',
          label: 'Engine import and artwork',
          status: validation.report.valid ? 'pass' : 'fail',
          detail:
            validation.report.reason ?? 'Definitions and artwork accepted by the game engine.',
        });
        const reportHash = await this.blobs.write(
          Buffer.from(JSON.stringify(validation.report)),
          'application/json',
        );
        await this.studio.artifact(row, {
          stage: 'checks',
          kind: 'report',
          label: 'Validation report',
          hash: reportHash,
        });
        if (!validation.report.valid)
          throw Error(validation.report.reason ?? 'Engine rejected the pack.');
        if (!validation.png) throw Error('Validation omitted its preview.');
        const previewHash = await this.blobs.write(validation.png, 'image/png');
        await this.studio.artifact(row, {
          stage: 'checks',
          kind: 'preview',
          label: 'Game-rendered scene and resource gallery',
          hash: previewHash,
        });
        await this.studio.stage(row, 'checks', 'complete');
        const hash = await this.blobs.write(bytes, 'application/json');
        await this.studio.stage(row, 'ready', 'complete');
        await this.studio.finish(row, {
          package: pack,
          hash,
          report: { ...validation.report, previewHash },
          simVersion: this.simVersion,
          text: plan.text,
          brief: plan.brief,
        });
        return;
      } catch (e) {
        if (e instanceof ProviderUncertain || e instanceof ProviderBudget || signal.aborted)
          throw e;
        error = e instanceof Error ? e.message : 'Invalid generated pack';
      }
      if (repair === 2) throw Error(`Generation failed after two repair passes: ${error}`);
      await this.studio.text(row, `Repair ${repair + 1}: ${error}`, repair + 1);
      const fixed = await attempts.run(row, `repair:${repair}`, textModel, { error, plan }, () =>
        this.provider.text(
          textModel,
          plannerPrompt(row.input.base, row.input.messages, plan.brief) +
            `\nRepair this proposed plan without expanding scope: ${JSON.stringify(plan)}\nValidation error: ${error}`,
          cfg.maxOutputTokens ?? 16000,
          signal,
        ),
      );
      const next = validatePlan(JSON.parse(fixed.text), row.input.base);
      // Repairs cannot delete extra entries, expand scope or turn a build into discussion.
      const keys = new Set(plan.entries.map((e) => entryKey(row.input.base, e.key)));
      if (
        next.action !== 'build' ||
        next.entries.length !== plan.entries.length ||
        next.entries.some(
          (e) =>
            !keys.has(entryKey(row.input.base, e.key)) ||
            e.operation !==
              plan.entries.find(
                (v) => entryKey(row.input.base, v.key) === entryKey(row.input.base, e.key),
              )?.operation ||
            e.kind !==
              plan.entries.find(
                (v) => entryKey(row.input.base, v.key) === entryKey(row.input.base, e.key),
              )?.kind,
        )
      )
        throw Error('Repair changed the requested scope.');
      plan = next;
    }
  }
}
