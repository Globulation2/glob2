import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useState } from 'react';
import { MeshPreview } from './MeshPreview.tsx';
import { ACTIONS, DEFAULT_CAMERA } from './geometry.ts';
import { MODELS, type Model } from './atlas.ts';
import { StudioDialog } from './StudioControls.tsx';
import { cloneSkin, paintCanvas, type SkinData } from './useSkinDocument.ts';

export function CopyPaintDialog({
  data,
  model,
  onClose,
  onApply,
}: {
  data: SkinData;
  model: Model;
  onClose: () => void;
  onApply: (data: SkinData) => void;
}) {
  useLocale();
  const [candidate] = useState(() => {
    const next = cloneSkin(data);
    for (const other of MODELS) {
      if (other.id === model.id) continue;
      for (let y = 0; y < 256; y++)
        for (let x = 0; x < 256; x++) {
          const from = (model.y + y) * 512 + model.x + x,
            to = (other.y + y) * 512 + other.x + x;
          next.materials[to] = data.materials[from] ?? 0;
          next.colour.set(data.colour.subarray(from * 4, from * 4 + 4), to * 4);
        }
    }
    return next;
  });
  const [canvas] = useState(() => paintCanvas(candidate.colour));
  return (
    <StudioDialog title={t('Copy paint to all models')} onClose={onClose} wide>
      <p>
        <RichMessage
          source={
            'Copies the {slot0} texture and materials. Different models wrap that paint differently; inspect the results below.'
          }
          slots={{ slot0: model.name.toLowerCase() }}
        />
      </p>
      <div className="skin-copy-grid">
        {MODELS.map((m) => (
          <div key={m.id}>
            <h3>{m.name}</h3>
            <MeshPreview
              texture={canvas}
              materials={candidate.materials}
              revision={1}
              model={m}
              swarmMesh={data.swarmMesh}
              camera={DEFAULT_CAMERA}
              action={ACTIONS[m.id][0] ?? ''}
              phase={0}
              interactive={false}
            />
          </div>
        ))}
      </div>
      <footer>
        <button onClick={onClose}>{t('Cancel')}</button>
        <button className="skin-primary" onClick={() => onApply(candidate)}>
          {t('Copy paint')}
        </button>
      </footer>
    </StudioDialog>
  );
}
