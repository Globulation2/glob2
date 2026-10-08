import { Icon } from '../../icons.tsx';
import type { Artifact, Stage, StageId } from './types.ts';

export function GenerationTimeline({
  stages,
  artifacts,
  selectedStage,
  enabled,
  select,
}: {
  stages: Stage[];
  artifacts: Artifact[];
  selectedStage: StageId;
  enabled: boolean;
  select: (id: StageId) => void;
}) {
  return (
    <ol className="ms-timeline" aria-label="Generation stages">
      {stages.map((s, i) => (
        <li key={s.id} data-state={s.status}>
          <button
            aria-label={s.label}
            aria-pressed={selectedStage === s.id && enabled}
            disabled={!enabled || s.status === 'pending'}
            onClick={() => select(s.id)}
          >
            <span className="ms-stage-icon">
              {s.status === 'complete' ? (
                <Icon name="check" size={18} />
              ) : s.status === 'failed' ? (
                <Icon name="alert-triangle" size={18} />
              ) : (
                String(i + 1).padStart(2, '0')
              )}
            </span>
            <span className="ms-stage-long">{s.label}</span>
            <span className="ms-stage-short">
              {
                {
                  prepare: 'Design',
                  terrain: 'Terrain',
                  build: 'Build',
                  checks: 'Checks',
                  ready: 'Ready',
                }[s.id]
              }
            </span>
            {artifacts.find((a) => a.stage === s.id) && (
              <img src={artifacts.find((a) => a.stage === s.id)?.url} alt="" />
            )}
          </button>
        </li>
      ))}
    </ol>
  );
}
