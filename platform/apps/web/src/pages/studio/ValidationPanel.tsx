import { t, useLocale, RichMessage, fixedCaption, statusLabel } from '../../i18n.tsx';
import { Icon } from '../../icons.tsx';
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
  useLocale();
  const failedCount = checks.filter((c) => c.status === 'failed').length;
  return (
    <details
      className="ms-validation"
      open={checking || checks.some((c) => c.status === 'failed') || undefined}
    >
      <summary>
        <span className={`ms-check-badge${checking ? ' ms-check-running' : ''}`} aria-hidden="true">
          <Icon
            name={
              checking
                ? 'loader-2'
                : checks.every((c) => c.status === 'passed')
                  ? 'check'
                  : 'alert-triangle'
            }
            size={18}
          />
        </span>
        <strong>
          {checking
            ? t('Checking your world…')
            : checks.every((c) => c.status === 'passed')
              ? t('Playability checks passed')
              : checks.some((c) => c.status === 'failed')
                ? t('{value0} {value1} attention', {
                    value0: failedCount,
                    value1: failedCount === 1 ? t('check needs') : t('checks need'),
                  })
                : t('Playability checks')}
        </strong>
        <span>
          <RichMessage
            source={'{slot0} / {slot1} passed'}
            slots={{
              slot0: checks.filter((c) => c.status === 'passed').length,
              slot1: checks.length,
            }}
          />
        </span>
      </summary>
      {(checking || checks.some((c) => c.status === 'failed')) && (
        <p className="ms-active-check">
          {fixedCaption(checks.find((c) => c.status === 'running' || c.status === 'failed')?.label)}
        </p>
      )}
      <ul>
        {checks.map((c) => (
          <li key={c.id} data-state={c.status}>
            <button aria-pressed={selected === c.id} onClick={() => select(c)}>
              <span aria-hidden="true">
                <Icon
                  name={
                    c.status === 'passed'
                      ? 'check'
                      : c.status === 'failed'
                        ? 'alert-triangle'
                        : 'loader-2'
                  }
                  size={18}
                />
              </span>
              <strong>{fixedCaption(c.label)}</strong>
              <span>{statusLabel(c.status)}</span>
            </button>
            {(selected === c.id || c.status === 'failed') && (
              <p>
                {c.detail ?? t('No additional details were recorded.')}
                {c.colony !== undefined && t(' Colony {value0}.', { value0: c.colony + 1 })}
                {c.location &&
                  t(' Location: {value0}, {value1}.', {
                    value0: c.location.x,
                    value1: c.location.y,
                  })}
              </p>
            )}
          </li>
        ))}
      </ul>
    </details>
  );
}
