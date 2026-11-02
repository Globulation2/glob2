// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { act, cleanup, render, screen } from '@testing-library/react';
import { COLONY_SKIN_MATERIALS, SWARM_MESHES } from '@glob2/protocol';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import tamil from '../../../packages/i18n/locales/ta.json';
import { LocaleProvider } from '../src/i18n.tsx';
import { MODELS } from '../src/skins/atlas.ts';
import { DEFAULT_CAMERA } from '../src/skins/geometry.ts';
import { SWARM_SHAPES } from '../src/skins/swarmShapes.ts';
import { MeshPreview } from '../src/skins/MeshPreview.tsx';
import {
  skinModelLabel,
  skinMaterialLabel,
  skinMaterialGroupLabel,
  skinShapeLabel,
  skinShapeDescription,
  skinPresetLabel,
  skinProductLabel,
} from '../src/skins/labels.ts';

vi.mock('../src/skins/materialShader.ts', () => ({ SKIN_MATERIAL_GLSL: '' }));
afterEach(async () => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  registerCatalog('ta', tamil);
  await setLocale('en');
});

it('translates every fixed registry label lazily without changing registry identities', async () => {
  await setLocale('en');
  const original = JSON.stringify({ MODELS, COLONY_SKIN_MATERIALS, SWARM_SHAPES });
  const sourceLabels = [
    ...MODELS.map((model) => skinModelLabel(model.id)),
    ...COLONY_SKIN_MATERIALS.flatMap((material) => [
      skinMaterialLabel(material.key),
      skinMaterialGroupLabel(material.group),
    ]),
    ...SWARM_MESHES.flatMap((id) => [skinShapeLabel(id), skinShapeDescription(id)]),
  ];
  expect(MODELS.map((model) => skinModelLabel(model.id))).toEqual(
    MODELS.map((model) => model.name),
  );
  for (const material of COLONY_SKIN_MATERIALS) {
    expect(skinMaterialLabel(material.key)).toBe(material.name);
    expect(skinMaterialGroupLabel(material.group)).toBe(material.group);
  }
  // Explicit fixture markers test locale switching, not translation wording.
  registerCatalog('ta', Object.fromEntries(sourceLabels.map((source) => [source, `ta:${source}`])));
  await setLocale('ta');
  for (const model of MODELS) expect(skinModelLabel(model.id)).toBe(`ta:${model.name}`);
  for (const material of COLONY_SKIN_MATERIALS) {
    expect(skinMaterialLabel(material.key)).toBe(`ta:${material.name}`);
    expect(skinMaterialGroupLabel(material.group)).toBe(`ta:${material.group}`);
  }
  for (const id of SWARM_MESHES) {
    expect(skinShapeLabel(id)).toBe(`ta:${SWARM_SHAPES[id].name}`);
    expect(skinShapeDescription(id)).toBe(`ta:${SWARM_SHAPES[id].description}`);
  }
  expect(JSON.stringify({ MODELS, COLONY_SKIN_MATERIALS, SWARM_SHAPES })).toBe(original);
  expect(skinProductLabel('custom-product', 'User product title')).toBe('User product title');
  expect(skinPresetLabel('custom-preset-id', 'A user-chosen name')).toBe('A user-chosen name');
});

it('keeps a failed 3D preview translatable when the open page changes language', async () => {
  const source =
    'Painting needs WebGL 2. You can still save your draft, browse My skins, or visit the shop.';
  registerCatalog('ta', tamil);
  await setLocale('en');
  vi.stubGlobal(
    'ResizeObserver',
    class {
      observe() {}
      disconnect() {}
    },
  );
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
  render(
    <LocaleProvider>
      <MeshPreview
        texture={document.createElement('canvas')}
        materials={new Uint8Array(512 * 512)}
        revision={0}
        model={MODELS[0]}
        swarmMesh="classic"
        camera={DEFAULT_CAMERA}
        action="walk"
        phase={0}
      />
    </LocaleProvider>,
  );
  expect(await screen.findByText(source)).toBeTruthy();
  await act(() => setLocale('ta'));
  expect(screen.getByText(tamil[source])).toBeTruthy();
  expect(screen.queryByText(source)).toBeNull();
});
