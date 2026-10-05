import type { Check } from './types.ts';
export function ValidationPanel({
  checks,
  checking,
  selected,
  select,
}: {
  checks: Check[];
  checking: boolean;
  selected?: string;
  select: (check: Check) => void;
}) {
  const failedCount = checks.filter((c) => c.status === 'failed').length;
  return (
    <details
      className="ms-validation"
      open={checking || checks.some((c) => c.status === 'failed') || undefined}
    >
      <summary>
        <span className={`ms-check-badge${checking ? ' ms-check-running' : ''}`} aria-hidden="true">
          {checking ? '◌' : checks.every((c) => c.status === 'passed') ? '✓' : '!'}
        </span>
        <strong>
          {checking
            ? 'Checking your world…'
            : checks.every((c) => c.status === 'passed')
              ? 'Playability checks passed'
              : checks.some((c) => c.status === 'failed')
                ? `${failedCount} ${failedCount === 1 ? 'check needs' : 'checks need'} attention`
                : 'Playability checks'}
        </strong>
        <span>
          {checks.filter((c) => c.status === 'passed').length} / {checks.length} passed
        </span>
      </summary>
      {(checking || checks.some((c) => c.status === 'failed')) && (
        <p className="ms-active-check">
          {checks.find((c) => c.status === 'running' || c.status === 'failed')?.label}
        </p>
      )}
      <ul>
        {checks.map((c) => (
          <li key={c.id} data-state={c.status}>
            <button aria-pressed={selected === c.id} onClick={() => select(c)}>
              <span aria-hidden="true">
                {c.status === 'passed'
                  ? '✓'
                  : c.status === 'failed'
                    ? '!'
                    : c.status === 'running'
                      ? '◌'
                      : '–'}
              </span>
              <strong>{c.label}</strong>
              <span>{c.status.replace('-', ' ')}</span>
            </button>
            {(selected === c.id || c.status === 'failed') && (
              <p>
                {c.detail ?? 'No additional details were recorded.'}
                {c.colony !== undefined && ` Colony ${c.colony + 1}.`}
                {c.location && ` Location: ${c.location.x}, ${c.location.y}.`}
              </p>
            )}
          </li>
        ))}
      </ul>
    </details>
  );
}
