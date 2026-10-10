import sharp from 'sharp';
import { isDeepStrictEqual } from 'node:util';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import {
  type BuildingAiStudio,
  HiveError,
  validatePlan,
  assemble,
  plannerPrompt,
  entryKey,
  type RequestRow,
  type EntryArtwork,
  type BuildingPlan,
} from '@glob2/building-studio';
import {
  type BuildingAiStudioConfig,
  type BuildingPackage,
  type BuildingStudioReport,
} from '@glob2/protocol';
import {
  readBuildingArchive,
  writeBuildingArchive,
  writeBuildingArtworkBundle,
  buildingPackageAssetHashes,
  buildingAssetHash,
} from '@glob2/protocol/node';
import type { AgentBlobs } from '@glob2/engine/blobs';
import type { BuildingCompositionResult } from '@glob2/engine/engineCli';
import {
  Attempts,
  ProviderUncertain,
  ProviderBudget,
  ProviderRejected,
  type BuildingProvider,
} from './provider.ts';
import { imagePrompt, processArtwork } from './artwork.ts';
import { stockReferences, referenceInstructions, type ArtworkReference } from './references.ts';
export interface Validator {
  composeBuildings(
    packages: readonly BuildingPackage[],
    signal?: AbortSignal,
    artwork?: Uint8Array,
  ): Promise<BuildingCompositionResult>;
}
/** Orchestrates frozen requests; DB leases and the provider journal own retries and settlement. */
export class Pipeline {
  readonly studio: BuildingAiStudio;
  readonly blobs: AgentBlobs;
  readonly provider: BuildingProvider;
  readonly engine: Validator;
  readonly config: BuildingAiStudioConfig;
  readonly root: string;
  readonly simVersion: string;
  constructor(
    studio: BuildingAiStudio,
    blobs: AgentBlobs,
    provider: BuildingProvider,
    engine: Validator,
    config: BuildingAiStudioConfig,
    root: string,
    simVersion: string,
  ) {
    this.studio = studio;
    this.blobs = blobs;
    this.provider = provider;
    this.engine = engine;
    this.config = config;
    this.root = root;
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
      imageModel = cfg.imageModel;
    if (!textModel || !imageModel || !cfg.providerCallsPerDay)
      throw Error('Building worker configuration is incomplete.');
    const attempts = new Attempts(
      this.studio,
      Math.min(cfg.providerCallsPerDay, this.config.providerCallsPerDay ?? cfg.providerCallsPerDay),
    );
    const original = readBuildingArchive(
      Buffer.from(await this.blobs.read(row.input.baseHash, 32 * 1024 * 1024)),
    );
    if (!isDeepStrictEqual(original.package, row.input.base))
      throw Error('Base archive does not match the saved request.');
    const reference = (
      await Promise.all(
        ['building-catalogs', 'building-semantics', 'building-authoring'].map((name) =>
          readFile(join(this.root, 'docs/features', name + '.md'), 'utf8'),
        ),
      )
    ).join('\n\n');
    const examples = await Promise.all(
      ['inn', 'hospital', 'defencetower', 'swarm'].map((name) =>
        readFile(join(this.root, 'data/buildings', name + '.json'), 'utf8'),
      ),
    );
    const prompt = plannerPrompt(
      row.input.base,
      row.input.messages,
      row.input.brief,
      reference + '\nStock definitions: ' + examples.join('\n'),
    );
    await this.studio.stage(row, 'prepare', 'running');
    const planned = await attempts.run(row, 'design', textModel, { prompt }, () =>
      this.provider.text(textModel, prompt, cfg.maxOutputTokens ?? 16000, signal),
    );
    let plan = validatePlan(JSON.parse(planned.text), row.input.base);
    if (plan.action === 'discuss') {
      await this.studio.stage(row, 'prepare', 'complete');
      await this.studio.finish(row, { text: plan.text, brief: plan.brief });
      return;
    }
    await this.studio.reserveBuild(row);
    await this.studio.stage(row, 'prepare', 'complete', plan.text);
    // User appearance references supplement, rather than displace, the game's camera references.
    const references: ArtworkReference[] = await Promise.all(
      row.input.submission.references.map(async (hash) => ({
        bytes: await this.blobs.read(hash, 8 * 1024 * 1024),
        role: 'player reference: desired appearance and materials; keep the stock game camera',
      })),
    );
    if (plan.entries.some((e) => e.operation === 'upsert' && e.regenerateArt))
      references.push(...(await stockReferences(this.root)));
    const art: Record<string, EntryArtwork> = {};
    for (let repair = 0; repair <= 1; repair++) {
      try {
        await this.studio.stage(row, 'artwork', 'running');
        // Generate completed structures before their sites. Recreate this order on every
        // repair/restart so journalled inputs remain identical and cannot cause duplicate calls.
        const entries = plan.entries.filter((e) => e.operation === 'upsert' && e.regenerateArt);
        const site = (e: BuildingPlan['entries'][number]) =>
          Number(
            JSON.parse(e.propertiesJson).isBuildingSite ??
              row.input.base.variants.find((v) => v.key === entryKey(row.input.base, e.key))
                ?.properties['isBuildingSite'] ??
              0,
          );
        entries.sort((a, b) => site(a) - site(b));
        const familySources = new Map<string, Uint8Array>();
        for (const [index, entry] of entries.entries()) {
          signal.throwIfAborted();
          const key = entryKey(row.input.base, entry.key),
            old = row.input.base.variants.find((v) => v.key === key);
          if (old?.properties['crossConnectMultiImage'])
            throw Error(
              'Connected artwork edits need all sixteen segment frames and are not supported yet.',
            );
          const props = { ...old?.properties, ...JSON.parse(entry.propertiesJson) };
          const oldSprite = row.input.base.sprites.find(
            (s) => 'package:' + s.key === old?.properties['gameSprite'],
          );
          // A sheet may serve many still variants. Only this variant's frame count denotes animation.
          if (Number(old?.properties['gameSpriteCount'] ?? 1) > 1)
            throw Error(
              'Animated artwork edits require manual authoring; existing frames can be preserved during property edits.',
            );
          const oldFrame = oldSprite?.frames[Number(old?.properties['gameSpriteImage'] ?? 0)];
          const oldBytes = oldSprite ? original.assets.get(oldFrame?.imageHash ?? '') : undefined;
          const oldTeam = oldSprite
            ? original.assets.get(oldFrame?.teamColorHash ?? '')
            : undefined;
          const oldReference = oldBytes
            ? await (oldTeam ? sharp(oldBytes).composite([{ input: oldTeam }]) : sharp(oldBytes))
                .png()
                .toBuffer()
            : undefined;
          const related = entry.next
            ? familySources.get(entryKey(row.input.base, entry.next))
            : undefined;
          const identity = related ?? oldReference ?? familySources.values().next().value;
          const inputs: ArtworkReference[] = identity
            ? [
                {
                  bytes: identity,
                  role: 'this building family: preserve identity, proportions, materials and camera across stages',
                },
                ...references,
              ]
            : references;
          const fullPrompt =
            imagePrompt(entry.artPrompt, entry.teamColor) + referenceInstructions(inputs);
          const imageInput = {
            prompt: fullPrompt,
            references: inputs.map((r) => buildingAssetHash(r.bytes)),
          };
          await this.studio.text(
            row,
            `Creating ${entry.key} (${index + 1}/${entries.length})`,
            repair + 1,
          );
          const source = await attempts.run(
            row,
            `art:${key}:${createHash('sha256').update(JSON.stringify(imageInput)).digest('hex')}`,
            imageModel,
            imageInput,
            async () => {
              const result = await this.provider.image(
                imageModel,
                fullPrompt,
                inputs.map((r) => r.bytes),
                true,
                signal,
              );
              return {
                hash: await this.blobs.write(result.bytes, 'image/png'),
                usage: result.usage,
                responseId: result.responseId,
              };
            },
          );
          await this.studio.artifact(row, {
            stage: 'artwork',
            kind: 'source',
            label: entry.key + ' source',
            hash: source.hash,
          });
          const local = 'variant-' + createHash('sha256').update(key).digest('hex').slice(0, 24);
          const sourceBytes = await this.blobs.read(source.hash, 16 * 1024 * 1024);
          familySources.set(key, sourceBytes);
          art[key] = await processArtwork(
            sourceBytes,
            local,
            Number(props.width ?? 2),
            Number(props.height ?? 2),
            entry.teamColor,
          );
        }
        await this.studio.stage(row, 'artwork', 'complete');
        await this.studio.stage(row, 'assemble', 'running');
        const pack = assemble(row.input.base, plan, art),
          assets = new Map(original.assets);
        for (const images of Object.values(art))
          for (const [hash, bytes] of images.assets) assets.set(hash, bytes);
        const required = buildingPackageAssetHashes(pack);
        for (const hash of assets.keys()) if (!required.has(hash)) assets.delete(hash);
        const archive = writeBuildingArchive(pack, assets),
          hash = await this.blobs.write(archive, 'application/zip');
        await this.studio.stage(row, 'assemble', 'complete');
        await this.studio.stage(row, 'checks', 'running');
        const composed = await this.engine.composeBuildings(
          [pack],
          signal,
          writeBuildingArtworkBundle([pack], assets),
        );
        const report: BuildingStudioReport = {
          valid: true,
          archiveHash: hash,
          baseHash: composed.baseHash,
          catalog: composed.catalog,
          suite: 1,
          ...(composed.artworkHash ? { artworkHash: composed.artworkHash } : {}),
        };
        await this.studio.check(row, {
          id: 'engine',
          label: 'Engine definitions and artwork',
          status: 'pass',
          detail: 'The game engine accepted this exact building archive.',
        });
        await this.studio.artifact(row, {
          stage: 'checks',
          kind: 'report',
          label: 'Native validation report',
          hash: await this.blobs.write(Buffer.from(JSON.stringify(report)), 'application/json'),
        });
        for (const variant of pack.variants) {
          const sprite = pack.sprites.find(
            (s) => 'package:' + s.key === variant.properties['gameSprite'],
          );
          if (sprite) {
            const bytes = assets.get(sprite.frames[0]?.imageHash ?? '');
            if (!bytes) throw Error('Preview image is missing.');
            await this.studio.artifact(row, {
              stage: 'checks',
              kind: 'preview',
              label: String(variant.presentation?.['displayName'] ?? variant.key),
              hash: await this.blobs.write(bytes, 'image/webp'),
            });
          }
        }
        // Retain validated manifests/artifacts even when a manual edit races delivery.
        await this.studio.stage(row, 'checks', 'complete');
        await this.studio.stage(row, 'ready', 'complete');
        await this.studio.finish(row, {
          package: pack,
          archive,
          hash,
          report,
          simVersion: this.simVersion,
          title: plan.title,
          text: plan.text,
          brief: plan.brief,
        });
        return;
      } catch (error) {
        if (
          error instanceof ProviderUncertain ||
          error instanceof ProviderBudget ||
          error instanceof ProviderRejected ||
          error instanceof HiveError ||
          signal.aborted
        )
          throw error;
        const reason = error instanceof Error ? error.message : 'Invalid generated building';
        await this.studio.check(row, {
          id: 'engine',
          label: 'Engine definitions and artwork',
          status: 'fail',
          detail: reason,
        });
        if (repair === 1)
          throw new Error('Generation failed after one repair pass: ' + reason, { cause: error });
        await this.studio.text(row, 'Repairing: ' + reason, 1);
        const fixed = await attempts.run(row, 'repair', textModel, { reason, plan }, () =>
          this.provider.text(
            textModel,
            prompt +
              `\nRepair without changing requested scope: ${JSON.stringify(plan)}\nValidation error: ${reason}`,
            cfg.maxOutputTokens ?? 16000,
            signal,
          ),
        );
        const next = validatePlan(JSON.parse(fixed.text), row.input.base);
        assertRepairScope(plan, next, row.input.base);
        plan = next;
      }
    }
  }
}
export function assertRepairScope(plan: BuildingPlan, next: BuildingPlan, base: BuildingPackage) {
  if (
    next.action !== 'build' ||
    next.scope !== plan.scope ||
    next.entries.length !== plan.entries.length ||
    next.experimentsJson !== plan.experimentsJson ||
    next.entries.some((e) => {
      const old = plan.entries.find((v) => entryKey(base, v.key) === entryKey(base, e.key));
      return (
        !old ||
        e.operation !== old.operation ||
        e.regenerateArt !== old.regenerateArt ||
        e.teamColor !== old.teamColor ||
        e.next !== old.next ||
        e.previous !== old.previous ||
        e.requiredExperiment !== old.requiredExperiment
      );
    })
  )
    throw Error('Repair changed the requested scope.');
}
