import { t, useLocale, RichMessage } from '../i18n.tsx';
/* Canvas creation validates the 2D context. */
/* eslint-disable @typescript-eslint/no-non-null-assertion */
import { useEffect, useRef, useState } from 'react';
import { ColorPicker } from './ColorPicker.tsx';
import { MeshPreview, type SceneView } from './MeshPreview.tsx';
import { StudioDialog, MaterialSwatches } from './StudioControls.tsx';
import { cloneSkin, paintCanvas, type SkinData } from './useSkinDocument.ts';
import {
  DEFAULT_PATTERN,
  buildFillChart,
  patternValue,
  padCoverage,
  type PatternKind,
} from './projection.ts';
import { applyCoverage } from './paint.ts';
import { type Camera } from './geometry.ts';
import { type Model, MATERIAL_GROUPS } from './atlas.ts';
export function PatternDialog({
  data,
  model,
  camera: initialCamera,
  action,
  phase,
  mode,
  color: initialColor,
  material: initialMaterial,
  onApply,
  onClose,
}: {
  data: SkinData;
  model: Model;
  camera: Camera;
  action: string;
  phase: number;
  mode: 'colour' | 'material';
  color: string;
  material: number;
  onApply: (data: SkinData) => void;
  onClose: () => void;
}) {
  useLocale();
  const [color, setColor] = useState(initialColor),
    [material, setMaterial] = useState(initialMaterial);
  const [base] = useState(() => cloneSkin(data)),
    [canvas] = useState(() => paintCanvas(data.colour));
  const [candidate, setCandidate] = useState(() => cloneSkin(data)),
    [revision, setRevision] = useState(0);
  const [camera, setCamera] = useState(() => ({ ...initialCamera, game: false }));
  const [options, setOptions] = useState(DEFAULT_PATTERN),
    [scene, setScene] = useState<SceneView | null>(null);
  const [background, setBackground] = useState(false),
    [secondColor, setSecondColor] = useState('#ffffff'),
    [secondMaterial, setSecondMaterial] = useState(0);
  const buildKey = JSON.stringify([
    camera,
    options,
    background,
    secondColor,
    secondMaterial,
    color,
    material,
    mode,
    scene?.width,
    scene?.height,
  ]);
  const [completedKey, setCompletedKey] = useState('');
  const projectionRef = useRef<SceneView | null>(null);
  const cameraReady = !!scene && JSON.stringify(scene.camera) === JSON.stringify(camera);
  useEffect(() => {
    if (!scene || !cameraReady) return;
    const timer = requestAnimationFrame(() => {
      const next = cloneSkin(base),
        projection = scene.projection(),
        chart = options.whole ? buildFillChart(scene.mesh, scene.view, model.id) : projection,
        coverage = new Float32Array(65536),
        behind = new Float32Array(65536);
      for (let i = 0; i < 65536; i++) {
        if (!(options.whole ? chart.used[i] : projection.visible[i])) continue;
        const x = chart.x[i] ?? 0;
        const y = chart.y[i] ?? 0;
        const value = patternValue(x, y, options);
        coverage[i] = value;
        behind[i] = 1 - value;
      }
      if (background)
        applyCoverage(
          next,
          model,
          padCoverage(behind, projection.used),
          mode,
          secondColor,
          secondMaterial,
        );
      applyCoverage(next, model, padCoverage(coverage, projection.used), mode, color, material);
      canvas
        .getContext('2d')!
        .putImageData(new ImageData(new Uint8ClampedArray(next.colour), 512, 512), 0, 0);
      setCandidate(next);
      setCompletedKey(buildKey);
      setRevision((r) => r + 1);
    });
    return () => cancelAnimationFrame(timer);
  }, [
    cameraReady,
    buildKey,
    base,
    canvas,
    scene,
    options,
    background,
    secondColor,
    secondMaterial,
    color,
    material,
    mode,
    model,
  ]);
  const patterns: [PatternKind, string][] = options.whole
    ? [
        ['solid', t('Solid')],
        ['stripes', t('Mirrored bands')],
        ['spots', t('Mirrored spots')],
        ['speckles', t('Mottled')],
      ]
    : [
        ['stripes', t('Stripes')],
        ['spots', t('Spots')],
        ['checker', t('Checker')],
        ['chevrons', t('Chevrons')],
        ['waves', t('Waves')],
        ['speckles', t('Speckles')],
      ];
  const solid = options.kind === 'solid';
  const bandPattern = ['stripes', 'chevrons', 'waves'].includes(options.kind);
  const densityLabel = bandPattern
    ? t('Band width')
    : options.kind === 'spots'
      ? t('Spot size')
      : t('Density');
  return (
    <StudioDialog title={t('Patterns & fills')} onClose={onClose} wide>
      <div className="skin-pattern-layout">
        <div className="skin-pattern-stage">
          <MeshPreview
            texture={canvas}
            materials={candidate.materials}
            revision={revision}
            model={model}
            swarmMesh={data.swarmMesh}
            camera={camera}
            onCamera={setCamera}
            action={action}
            phase={phase}
            tool="orbit"
            onScene={(s) => {
              if (projectionRef.current !== s) {
                projectionRef.current = s;
                setScene(s);
              }
            }}
          />
          <span className="skin-stage-caption">
            <RichMessage
              source={'Drag to choose your {slot0} angle'}
              slots={{ slot0: options.whole ? t('inspection') : t('projection') }}
            />
          </span>
        </div>
        <div className="skin-pattern-controls">
          <div className="skin-segment" role="group" aria-label={t('Pattern coverage')}>
            <button
              aria-pressed={!options.whole}
              onClick={() => setOptions({ ...DEFAULT_PATTERN })}
            >
              {t('From this view')}
            </button>
            <button
              aria-pressed={options.whole}
              onClick={() =>
                setOptions({ ...DEFAULT_PATTERN, whole: true, kind: 'solid', scale: 64 })
              }
            >
              {t('Whole model')}
            </button>
          </div>
          {mode === 'colour' ? (
            <ColorPicker label={t('Pattern color')} value={color} onChange={setColor} />
          ) : (
            <MaterialSwatches color={data.building} selected={material} onSelect={setMaterial} />
          )}
          <div className="skin-pattern-grid" data-whole={options.whole}>
            {patterns.map(([id, label]) => (
              <button
                key={id}
                aria-pressed={options.kind === id}
                onClick={() => setOptions({ ...options, kind: id })}
              >
                <span className={`skin-pattern-tile pattern-${id}`} />
                {label}
              </button>
            ))}
          </div>
          {!solid &&
            (options.whole ? (
              <label>
                {t('Pattern size')}
                <select
                  value={options.scale}
                  onChange={(e) => setOptions({ ...options, scale: Number(e.target.value) })}
                >
                  <option value={32}>{t('Small')}</option>
                  <option value={64}>{t('Medium')}</option>
                  <option value={96}>{t('Large')}</option>
                </select>
              </label>
            ) : (
              <>
                <label>
                  {t('Scale')}
                  <input
                    type="range"
                    min={16}
                    max={160}
                    value={options.scale}
                    onChange={(e) => setOptions({ ...options, scale: Number(e.target.value) })}
                  />
                </label>
                <label>
                  {t('Rotation ')}
                  <span className="skin-value">
                    <RichMessage source={'{slot0}°'} slots={{ slot0: options.rotation }} />
                  </span>
                  <input
                    type="range"
                    aria-label={t('Pattern rotation')}
                    min={0}
                    max={359}
                    value={options.rotation}
                    onChange={(e) => setOptions({ ...options, rotation: Number(e.target.value) })}
                  />
                </label>
                <label>
                  {t('Across')}
                  <input
                    type="range"
                    min={-160}
                    max={160}
                    value={options.offsetX}
                    onChange={(e) => setOptions({ ...options, offsetX: Number(e.target.value) })}
                  />
                </label>
                <label>
                  {t('Up / down')}
                  <input
                    type="range"
                    min={-160}
                    max={160}
                    value={options.offsetY}
                    onChange={(e) => setOptions({ ...options, offsetY: Number(e.target.value) })}
                  />
                </label>
              </>
            ))}
          {!solid &&
            options.kind !== 'checker' &&
            (options.whole ? (
              <label>
                {densityLabel}
                <select
                  value={options.density}
                  onChange={(e) => setOptions({ ...options, density: Number(e.target.value) })}
                >
                  <option value={0.3}>
                    {bandPattern ? t('Thin') : options.kind === 'spots' ? t('Small') : t('Light')}
                  </option>
                  <option value={0.5}>{t('Balanced')}</option>
                  <option value={0.7}>
                    {bandPattern ? t('Wide') : options.kind === 'spots' ? t('Large') : t('Dense')}
                  </option>
                </select>
              </label>
            ) : (
              <label>
                {densityLabel}
                <input
                  type="range"
                  min={0.1}
                  max={0.9}
                  step={0.05}
                  value={options.density}
                  onChange={(e) => setOptions({ ...options, density: Number(e.target.value) })}
                />
              </label>
            ))}
          {options.kind === 'speckles' && (
            <button onClick={() => setOptions({ ...options, seed: options.seed + 1 })}>
              <RichMessage source={'Shuffle pattern · {slot0}'} slots={{ slot0: options.seed }} />
            </button>
          )}
          {!solid && (
            <label className="skin-check">
              <input
                type="checkbox"
                checked={background}
                onChange={(e) => setBackground(e.target.checked)}
              />
              {t('Replace the gaps too')}
            </label>
          )}
          {!solid &&
            background &&
            (mode === 'colour' ? (
              <ColorPicker
                label={t('Background color')}
                value={secondColor}
                onChange={setSecondColor}
              />
            ) : (
              <label>
                {t('Background material')}
                <select
                  value={secondMaterial}
                  onChange={(e) => setSecondMaterial(Number(e.target.value))}
                >
                  {MATERIAL_GROUPS.map((group) => (
                    <optgroup key={group.name} label={group.name}>
                      {group.materials.map((m) => (
                        <option key={m.id} value={m.id}>
                          {m.name}
                        </option>
                      ))}
                    </optgroup>
                  ))}
                </select>
              </label>
            ))}
          <p className="skin-muted">
            <RichMessage
              source={'{slot0} The preview shows exactly what will be saved.'}
              slots={{
                slot0: options.whole
                  ? t('Fills repeat across the model’s matching surfaces.')
                  : t('Projects onto visible surfaces. Shared surfaces repeat the paint.'),
              }}
            />
          </p>
        </div>
      </div>
      <footer>
        <button onClick={onClose}>{t('Cancel')}</button>
        <button
          className="skin-primary"
          disabled={!cameraReady || completedKey !== buildKey}
          onClick={() => onApply(candidate)}
        >
          <RichMessage source={'Apply to {slot0}'} slots={{ slot0: model.name }} />
        </button>
      </footer>
    </StudioDialog>
  );
}
