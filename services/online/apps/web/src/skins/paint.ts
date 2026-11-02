import type { SkinData } from './useSkinDocument.ts';
import type { Model } from './atlas.ts';

export function applyCoverage(
  data: SkinData,
  model: Model,
  coverage: Float32Array,
  mode: 'colour' | 'material',
  color: string,
  material: number,
  opacity = 1,
) {
  const rgb = [1, 3, 5].map((i) => parseInt(color.slice(i, i + 2), 16));
  for (let i = 0; i < coverage.length; i++) {
    const alpha = (coverage[i] ?? 0) * opacity;
    if (alpha <= 0) continue;
    const at = (model.y + Math.floor(i / 256)) * 512 + model.x + (i % 256);
    if (mode === 'material') {
      if (alpha >= 0.5) data.materials[at] = material;
    } else
      for (let c = 0; c < 3; c++)
        data.colour[at * 4 + c] = Math.round(
          (data.colour[at * 4 + c] ?? 255) * (1 - alpha) + (rgb[c] ?? 255) * alpha,
        );
  }
}
