import { useState } from 'react';
import type { AdminCrashGroup, AdminCrashReport, CrashFrame } from '@glob2/protocol';
import { formatDate, formatNumber, t, useLocale } from '../i18n.tsx';
import { api } from '../api.ts';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { PageControls, useAdminFilters, useFilterDraft } from './filters.tsx';
import { Report } from './ProblemReports.tsx';

// Crash groups from the crash worker (docs/multiplayer/crash-reports.md):
// reports whose top in-game frames match, sorted by affected installs, and
// each group's reports with their symbolicated stacks. Administrators only.
export function Crashes() {
  useLocale();
  const { values, set } = useAdminFilters();
  const group = values['group'];
  if (group)
    return (
      <>
        <div className="toolbar">
          <button onClick={() => set({ group: undefined })}>{t('Back')}</button>
        </div>
        <GroupReports group={group} />
      </>
    );
  return <Groups />;
}

const FILTERS = [
  { key: 'version', name: 'Version' },
  { key: 'platform', name: 'Platform' },
  { key: 'distribution', name: 'Channel' },
] as const;

function Groups() {
  const { values, set } = useAdminFilters();
  const [draft, setDraft] = useFilterDraft(
    Object.fromEntries(FILTERS.map((f) => [f.key, values[f.key] ?? ''])),
  );
  const query = Object.fromEntries(
    ['version', 'platform', 'distribution', 'cursor'].flatMap((key) =>
      values[key] ? [[key, values[key]]] : [],
    ),
  );
  const load = useLoad((signal) => api.adminCrashGroups(query, signal), [JSON.stringify(query)]);
  return (
    <>
      <form
        className="toolbar"
        role="search"
        onSubmit={(e) => {
          e.preventDefault();
          set(Object.fromEntries(FILTERS.map((f) => [f.key, draft[f.key]?.trim()])));
        }}
      >
        {FILTERS.map((filter) => (
          <input
            key={filter.key}
            aria-label={t(filter.name)}
            placeholder={t(filter.name)}
            value={draft[filter.key] ?? ''}
            onChange={(e) => setDraft({ ...draft, [filter.key]: e.target.value })}
          />
        ))}
        <button type="submit">{t('Filter')}</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            {page.items.length === 0 && <p>{t('No crashes yet.')}</p>}
            {page.items.map((group) => (
              <Group key={group.id} group={group} />
            ))}
            <PageControls nextCursor={page.nextCursor} />
          </>
        )}
      </Loaded>
    </>
  );
}

function Group({ group }: { group: AdminCrashGroup }) {
  const { set } = useAdminFilters();
  return (
    <article className="card" aria-labelledby={`crash-${group.id}`}>
      <h3 id={`crash-${group.id}`}>
        <code style={{ overflowWrap: 'anywhere' }}>{group.signature}</code>
      </h3>
      <dl>
        <dt>{t('Installs')}</dt>
        <dd>{formatNumber(group.installs)}</dd>
        <dt>{t('Reports')}</dt>
        <dd>{formatNumber(group.reports)}</dd>
        <dt>{t('Releases')}</dt>
        <dd>
          <code>
            {group.firstVersion === group.lastVersion
              ? group.firstVersion
              : [group.firstVersion, group.lastVersion].join(' – ')}
          </code>
        </dd>
        <dt>{t('Platform')}</dt>
        <dd>
          <code>{[...group.platforms, ...group.distributions].join(' · ')}</code>
        </dd>
        <dt>{t('Updated')}</dt>
        <dd>
          <time dateTime={group.lastSeenAt}>
            {formatDate(group.lastSeenAt, { dateStyle: 'medium', timeStyle: 'short' })}
          </time>
        </dd>
      </dl>
      <button onClick={() => set({ group: group.id })}>{t('Show reports')}</button>
    </article>
  );
}

function GroupReports({ group }: { group: string }) {
  const { values } = useAdminFilters();
  const query = { group, ...(values['cursor'] ? { cursor: values['cursor'] } : {}) };
  const load = useLoad((signal) => api.adminCrashReports(query, signal), [JSON.stringify(query)]);
  return (
    <Loaded load={load}>
      {(page) => (
        <>
          {page.items.map((report, index) => (
            <div key={report.id}>
              <Report report={report} />
              <StackDetails report={report} open={index === 0} />
            </div>
          ))}
          <PageControls nextCursor={page.nextCursor} />
        </>
      )}
    </Loaded>
  );
}

/** A report's stack, loaded when first opened. */
function StackDetails({ report, open }: { report: AdminCrashReport; open: boolean }) {
  const [shown, setShown] = useState(open);
  return (
    <details open={open} onToggle={(e) => setShown(shown || e.currentTarget.open)}>
      <summary>{t('Stack')}</summary>
      {shown && <Stack id={report.id} />}
    </details>
  );
}

function Stack({ id }: { id: string }) {
  const load = useLoad((signal) => api.adminCrashReport(id, signal), [id]);
  return <Loaded load={load}>{(detail) => <Frames frames={detail.frames} />}</Loaded>;
}

export function Frames({ frames }: { frames: CrashFrame[] }) {
  return (
    <ol start={0} style={{ fontFamily: 'var(--font-mono, monospace)', overflowWrap: 'anywhere' }}>
      {frames.map((frame, index) => (
        <li key={index} style={frame.inGame ? { fontWeight: 600 } : undefined}>
          {frame.function ?? `${frame.module}+${frame.offset}`}
          {frame.file && (
            <span style={{ opacity: 0.7 }}>
              {' '}
              {[frame.file.split(/[/\\]/).slice(-2).join('/'), frame.line]
                .filter((x) => x !== undefined)
                .join(':')}
            </span>
          )}
        </li>
      ))}
    </ol>
  );
}
