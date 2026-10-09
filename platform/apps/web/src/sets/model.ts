import { t } from '../messages.ts';
export {
  TERRAIN_PRESETS,
  MATERIALS,
  RESOURCE_PRESETS,
  TERRAIN_PROPERTIES,
  namespace,
  newPackage,
  newRelease,
} from '@glob2/protocol';
export async function sheetFromFile(file: File, frameWidth: number, frameHeight: number) {
  if (file.size > 12 * 1024 * 1024) throw Error(t('This PNG exceeds the sheet upload limit.'));
  if (
    !Number.isInteger(frameWidth) ||
    !Number.isInteger(frameHeight) ||
    frameWidth < 1 ||
    frameHeight < 1 ||
    frameWidth > 64 ||
    frameHeight > 64
  )
    throw Error(t('Frame dimensions must be whole numbers between 1 and 64.'));
  const bytes = new Uint8Array(await file.arrayBuffer());
  if (bytes.length < 33 || [137, 80, 78, 71, 13, 10, 26, 10].some((v, i) => bytes[i] !== v))
    throw Error(t('Choose a PNG spritesheet.'));
  const view = new DataView(bytes.buffer),
    width = view.getUint32(16),
    height = view.getUint32(20);
  if (
    !width ||
    !height ||
    width > 2048 ||
    height > 2048 ||
    width % frameWidth ||
    height % frameHeight
  )
    throw Error(t('The sheet must be at most 2048×2048 and fit its frame grid exactly.'));
  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (v) =>
    v.toString(16).padStart(2, '0'),
  ).join('');
  let binary = '';
  for (let i = 0; i < bytes.length; i += 8192)
    binary += String.fromCharCode(...bytes.subarray(i, i + 8192));
  return {
    hash,
    png: btoa(binary).replaceAll('+', '-').replaceAll('/', '_').replaceAll('=', ''),
    frameWidth,
    frameHeight,
  };
}
export function sheetUrl(png: string) {
  return 'data:image/png;base64,' + png.replaceAll('-', '+').replaceAll('_', '/');
}
