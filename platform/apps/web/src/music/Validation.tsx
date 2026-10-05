import { parse, MusicStudioCheck } from '@glob2/protocol';
/** Public releases contain only final trusted checks, never authoring artifacts. */
export function MusicValidation({ checks }: { checks: unknown[] }) {
  const valid = checks.flatMap((value) => {
    try {
      return [parse(MusicStudioCheck, value)];
    } catch {
      return [];
    }
  });
  return (
    <section className="music-validation">
      <h2>Automated validation</h2>
      <p>Measured on the delivered audio. These checks do not replace listening.</p>
      {valid.map((check) => (
        <MusicCheckDetails key={check.id} check={check} />
      ))}
    </section>
  );
}

/** Keep public delivery and private attempt measurements consistent. */
export function MusicCheckDetails({
  check,
  showAttempt = false,
}: {
  check: MusicStudioCheck;
  showAttempt?: boolean;
}) {
  return (
    <details className="music-check" data-status={check.status}>
      <summary>
        <span className={`music-check-status ${check.status}`}>{check.status}</span>
        <strong>{check.label}</strong>
        {showAttempt && <small>Candidate {check.attempt}</small>}
      </summary>
      {check.detail && <p>{check.detail}</p>}
      {check.measures.map((measure, index) => (
        <div className="music-measure" key={`${measure.name}:${index}`}>
          <strong>{measure.name}</strong>
          <span>
            {measure.status} ·{' '}
            {typeof measure.value === 'object'
              ? JSON.stringify(measure.value)
              : String(measure.value ?? '—')}{' '}
            {measure.unit}
          </span>
          <small>{measure.threshold}</small>
          <p>{measure.detail}</p>
        </div>
      ))}
    </details>
  );
}
