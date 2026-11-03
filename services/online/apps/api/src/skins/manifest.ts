import type { ApiServices } from '../services.ts';
import type { ColonySkinVersion } from '@glob2/protocol';
import { webpRendition } from '../http/webpRendition.ts';
import { sha256Hex } from '@glob2/core';
import { SWARM_MESHES, type SwarmMeshId } from '@glob2/protocol';

/** The content a colony skin version publishes; its manifest hash identifies it. */
export interface SkinContent {
  skinId: string;
  textureSha256: string;
  materialSha256: string;
  layout: 'colony-v2';
  buildingColor: number;
  swarmMesh: SwarmMeshId;
  swarmViewAngle?: number;
}

/**
 * The manifest hash that game clients recompute (src/online/SkinAuthorization.cpp)
 * before showing a skin, so field order and spelling are part of the contract.
 * Classic swarm skins leave swarmMesh out, and clients that do not know other
 * meshes reject those skins instead of painting them onto the classic swarm.
 */
export function skinManifestSha256(content: SkinContent): string {
  const manifest: Record<string, string | number> = {
    skinId: content.skinId,
    textureSha256: content.textureSha256,
    materialSha256: content.materialSha256,
    layout: content.layout,
    buildingColor: content.buildingColor,
  };
  if (content.swarmMesh !== 'classic') manifest['swarmMesh'] = content.swarmMesh;
  if (content.swarmViewAngle) manifest['swarmViewAngle'] = content.swarmViewAngle;
  return sha256Hex(Buffer.from(JSON.stringify(manifest)));
}

/**
 * A stored swarm mesh id, or undefined for one this API does not know: the
 * database accepts any well-formed id, so a rolled-back release can meet rows a
 * newer one wrote.
 */
export function knownSwarmMesh(value: string): SwarmMeshId | undefined {
  return SWARM_MESHES.find((mesh) => mesh === value);
}

/** Wire hashes describe WebP renditions; the published version and source remain immutable. */
export async function webpSkinVersion(
  services: Pick<ApiServices, 'db' | 'blobs'>,
  version: ColonySkinVersion,
): Promise<ColonySkinVersion> {
  const [textureSha256, materialSha256] = await Promise.all([
    webpRendition(services.db, services.blobs, version.textureSha256),
    webpRendition(services.db, services.blobs, version.materialSha256),
  ]);
  const wire = { ...version, textureSha256, materialSha256 };
  return { ...wire, manifestSha256: skinManifestSha256(wire) };
}
