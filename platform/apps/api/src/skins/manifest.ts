import { sha256Hex } from '@glob2/core';
import { SWARM_MESHES, type SwarmMeshId } from '@glob2/protocol';

/** The content a colony skin version publishes; its manifest hash identifies it. */
export interface SkinContent {
  skinId: string;
  textureSha256: string;
  layout: 'colony-v1';
  buildingColor: number;
  swarmMesh: SwarmMeshId;
}

/**
 * The manifest hash that game clients recompute (src/online/SkinAuthorization.cpp)
 * before showing a skin, so field order and spelling are part of the contract.
 * Classic swarm skins leave swarmMesh out: versions published before mesh choice
 * keep their hash, and older clients, which do not know other meshes, reject
 * those skins instead of painting them onto the classic swarm.
 */
export function skinManifestSha256(content: SkinContent): string {
  const manifest: Record<string, string | number> = {
    skinId: content.skinId,
    textureSha256: content.textureSha256,
    layout: content.layout,
    buildingColor: content.buildingColor,
  };
  if (content.swarmMesh !== 'classic') manifest['swarmMesh'] = content.swarmMesh;
  return sha256Hex(Buffer.from(JSON.stringify(manifest)));
}

/** A stored swarm mesh id, which the API only ever writes from the catalog. */
export function storedSwarmMesh(value: string): SwarmMeshId {
  const id = SWARM_MESHES.find((mesh) => mesh === value);
  if (!id) throw new Error(`Unknown stored swarm mesh: ${value}`);
  return id;
}
