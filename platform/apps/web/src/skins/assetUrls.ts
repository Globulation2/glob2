import type { ColonySkinVersion } from '@glob2/protocol';

/** Content hashes keep immutable renditions separate from older wire encodings. */
export function skinAssetUrl(version: ColonySkinVersion, asset: 'texture' | 'material') {
  const hash = asset === 'texture' ? version.textureSha256 : version.materialSha256;
  return `/api/v1/skins/versions/${version.id}/${asset}?sha256=${hash}`;
}
