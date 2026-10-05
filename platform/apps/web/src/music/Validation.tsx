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
        <details key={check.id}>
          <summary>
            {check.label}: {check.status}
          </summary>
          {check.measures.map((m, i) => (
            <div key={i}>
              <strong>{m.name}</strong>
              <p>
                {m.status} ·{' '}
                {typeof m.value === 'object' ? JSON.stringify(m.value) : String(m.value ?? '—')}{' '}
                {m.unit}
              </p>
              <p>{m.threshold}</p>
              <p>{m.detail}</p>
            </div>
          ))}
        </details>
      ))}
    </section>
  );
}
