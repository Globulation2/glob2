import { canonicalMaterialMap, canonicalSkinImage } from './images.ts';
import { readFile } from 'node:fs/promises';
import { putContent } from '@glob2/core';
import type { ApiServices } from '../services.ts';
import { skinManifestSha256, type SkinContent } from './manifest.ts';

/** Stable IDs and immutable PNG bytes (colour atlas `<sku>.png`, material map
 * `<sku>-material.png`): never overwrite a published preset. */
export const PRESETS = [
  {
    sku: 'stripes',
    name: 'Colony stripes',
    skinId: '97b6e116-918a-490c-a59d-c9a7a3e97201',
    versionId: '97b6e116-918a-490c-a59d-c9a7a3e97301',
  },
  {
    sku: 'spots',
    name: 'Colony spots',
    skinId: '97b6e116-918a-490c-a59d-c9a7a3e97202',
    versionId: '97b6e116-918a-490c-a59d-c9a7a3e97302',
  },
] as const;
export async function seedSkinPresets({ db, blobs }: Pick<ApiServices, 'db' | 'blobs'>) {
  for (const preset of PRESETS) {
    const asset = (name: string) =>
      readFile(new URL(`../../assets/skins/${name}.png`, import.meta.url));
    const texture = await putContent(
      blobs,
      await canonicalSkinImage((await asset(preset.sku)).toString('base64')),
    );
    const material = await putContent(
      blobs,
      await canonicalMaterialMap((await asset(`${preset.sku}-material`)).toString('base64')),
    );
    const content: SkinContent = {
      skinId: preset.skinId,
      textureSha256: texture.sha256,
      materialSha256: material.sha256,
      layout: 'colony-v2',
      buildingColor: 0x2d73b4,
      swarmMesh: 'classic',
    };
    await db.transaction().execute(async (trx) => {
      for (const stored of [texture, material])
        await trx
          .insertInto('blobs')
          .values({
            sha256: stored.sha256,
            size: stored.size,
            storage_key: stored.key,
            content_type: 'image/webp',
            visibility: 'public',
          })
          .onConflict((oc) => oc.column('sha256').doNothing())
          .execute();
      await trx
        .insertInto('colony_skins')
        .values({
          id: preset.skinId,
          name: preset.name,
          kind: 'preset',
          entitlement: `skins:${preset.sku}`,
        })
        .onConflict((oc) => oc.column('id').doNothing())
        .execute();
      await trx
        .insertInto('colony_skin_versions')
        .values({
          id: preset.versionId,
          skin_id: preset.skinId,
          texture_sha256: texture.sha256,
          material_sha256: material.sha256,
          layout: content.layout,
          building_color: content.buildingColor,
          swarm_mesh: content.swarmMesh,
          manifest_sha256: skinManifestSha256(content),
        })
        .onConflict((oc) => oc.column('id').doNothing())
        .execute();
    });
  }
}
