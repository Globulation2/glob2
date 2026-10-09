import { statusLabel } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { parse, MusicStudioCheck } from '@glob2/protocol';
const CATEGORIES: Record<string, { label: string; guidance: string }> = {
  score: { label: 'Composition', guidance: 'Listen for the musical structure and development.' },
  format: { label: 'Playback format', guidance: 'Check that all three moods play correctly.' },
  loudness: {
    label: 'Consistent levels',
    guidance: 'Listen for unexpected jumps in volume between moods.',
  },
  seam: {
    label: 'Loop continuity',
    guidance: 'Listen for a click, gap, or abrupt change when the loop repeats.',
  },
  repetition: {
    label: 'Musical variety',
    guidance: 'Listen for short phrases that repeat too mechanically.',
  },
  alignment: {
    label: 'Mood synchronization',
    guidance: 'Switch moods and listen for changes in timing or harmony.',
  },
  contrast: {
    label: 'Mood contrast',
    guidance: 'Check that building and combat feel distinct from calm.',
  },
  noise: {
    label: 'Background noise',
    guidance: 'Listen for hiss or scratchiness in quiet passages.',
  },
  balance: { label: 'Mix balance', guidance: 'Listen for muddy bass or harsh high frequencies.' },
  audibility: {
    label: 'Quiet passage audibility',
    guidance: 'Check that calm passages remain audible alongside game sound.',
  },
  dropout: { label: 'Sustained notes', guidance: 'Listen for notes that cut off unexpectedly.' },
};
const ICONS: Record<MusicStudioCheck['status'], string> = {
  pass: '✓',
  warn: '⚠',
  fail: '×',
  waived: '↪',
  skip: '—',
  info: 'i',
  pending: '○',
  running: '◉',
};
function category(check: MusicStudioCheck) {
  const key = check.label.toLowerCase();
  return Object.hasOwn(CATEGORIES, key) ? CATEGORIES[key] : undefined;
}
function categoryLabel(check: MusicStudioCheck) {
  const value = category(check);
  return value ? t(value.label) : check.label;
}
/** Repeated candidates belong in history, never in the current quality summary. */
export function latestMusicChecks(checks: MusicStudioCheck[]) {
  const latest = new Map<string, MusicStudioCheck>();
  for (const check of checks) {
    const key = check.label.toLowerCase();
    const previous = latest.get(key);
    if (!previous || check.attempt >= previous.attempt) latest.set(key, check);
  }
  return [...latest.values()];
}
export function MusicValidation({
  checks,
  warnings = [],
  latestAttempts = false,
}: {
  checks: unknown[];
  warnings?: string[];
  latestAttempts?: boolean;
}) {
  useLocale();
  const parsed = checks.flatMap((value) => {
    try {
      return [parse(MusicStudioCheck, value)];
    } catch {
      return [];
    }
  });
  const valid = latestAttempts ? latestMusicChecks(parsed) : parsed;
  const attention = valid.filter((c) => c.status === 'warn' || c.status === 'fail');
  const passed = valid.filter((c) => c.status === 'pass').length;
  const otherStatuses = ['waived', 'skip', 'info', 'pending', 'running'] as const;
  const otherSummary = otherStatuses
    .map((status) => {
      const count = valid.filter((check) => check.status === status).length;
      return count
        ? t('{count} {status}', {
            count,
            status: statusLabel(status === 'skip' ? 'skipped' : status),
          })
        : '';
    })
    .filter(Boolean)
    .join(' · ');
  const visibleDetails = new Set(
    attention
      .map(
        (c) =>
          c.detail ?? c.measures.find((m) => m.status === 'warn' || m.status === 'fail')?.detail,
      )
      .filter(Boolean),
  );
  const notes = [...new Set(warnings)].filter((warning) => !visibleDetails.has(warning));
  return (
    <section className="music-validation">
      <div className="music-quality-heading">
        <div>
          <span className="music-eyebrow">{t('THE LISTENING NOTES')}</span>
          <h2>{t('Audio quality checks')}</h2>
        </div>
        <span className="music-quality-count">
          <RichMessage
            source={'{slot0} passed · {slot1}  {slot2}{slot3}'}
            slots={{
              slot0: passed,
              slot1: attention.length,
              slot2: attention.length === 1 ? t('finding') : t('findings'),
              slot3: otherSummary ? ` · ${otherSummary}` : '',
            }}
          />
        </span>
      </div>
      <p>
        {t('Measurements flag possible defects. Your ears decide whether the music feels right.')}
      </p>
      {!valid.length && <p>{t('No measured results are available for this version.')}</p>}
      {checks.length !== parsed.length && <p>{t('Some results could not be displayed.')}</p>}
      {!!attention.length && (
        <div className="music-quality-findings">
          {attention.map((check) => (
            <article key={check.id} data-status={check.status}>
              <div>
                <span className={`music-check-status ${check.status}`}>
                  <span aria-hidden="true">{ICONS[check.status]}</span>{' '}
                  {check.status === 'fail' ? t('Failed') : t('Listening note')}
                </span>
                <strong>{categoryLabel(check)}</strong>
              </div>
              <p>
                {check.detail ??
                  check.measures.find((m) => m.status === 'warn' || m.status === 'fail')?.detail ??
                  t('Review the technical results for this finding.')}
              </p>
              {category(check)?.guidance && <small>{category(check)?.guidance}</small>}
            </article>
          ))}
        </div>
      )}
      {!!notes.length && (
        <ul className="music-listening-notes">
          {notes.map((note) => (
            <li key={note}>{note}</li>
          ))}
        </ul>
      )}
      {!!valid.length && (
        <details className="music-technical">
          <summary>
            <RichMessage
              source={'Show technical results {slot0}'}
              slots={{
                slot0: (
                  <span>
                    {valid.length} {t(' checks')}
                  </span>
                ),
              }}
            />
          </summary>
          {valid.map((check) => (
            <MusicCheckDetails key={check.id} check={check} showAttempt={latestAttempts} />
          ))}
        </details>
      )}
    </section>
  );
}
export function MusicCheckDetails({
  check,
  showAttempt = false,
}: {
  check: MusicStudioCheck;
  showAttempt?: boolean;
}) {
  useLocale();
  return (
    <details className="music-check" data-status={check.status}>
      <summary>
        <span className={`music-check-status ${check.status}`}>
          <span aria-hidden="true">{ICONS[check.status]}</span> {statusLabel(check.status)}
        </span>
        <strong>{categoryLabel(check)}</strong>
        {showAttempt && (
          <small>
            <RichMessage source={'Candidate {slot0}'} slots={{ slot0: check.attempt }} />
          </small>
        )}
      </summary>
      {check.detail && <p>{check.detail}</p>}
      {check.measures.map((measure, index) => (
        <div className="music-measure" key={`${measure.name}:${index}`}>
          <strong>{measure.name}</strong>
          <span>
            <RichMessage
              source={'{slot0} · {slot1} {slot2}'}
              slots={{
                slot0: statusLabel(measure.status),
                slot1:
                  typeof measure.value === 'object'
                    ? JSON.stringify(measure.value)
                    : String(measure.value ?? '—'),
                slot2: measure.unit,
              }}
            />
          </span>
          <small>{measure.threshold}</small>
          <p>{measure.detail}</p>
        </div>
      ))}
    </details>
  );
}
