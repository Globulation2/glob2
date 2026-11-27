import { useState } from 'react';
import type {
  AdminCrashGroup,
  AdminCrashRelease,
  AdminCrashReport,
  CrashFrame,
  CrashFreeStats,
  CrashGroupState,
} from '@glob2/protocol';
import { displayMessage, formatDate, formatNumber, t, useLocale } from '../i18n.tsx';
import { api } from '../api.ts';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { PageControls, useAdminFilters, useFilterDraft } from './filters.tsx';
import { Report } from './ProblemReports.tsx';

// Crash groups from the crash worker (docs/multiplayer/crash-reports.md):
// reports whose top in-game frames match, sorted by affected installs, with
// their triage state, and each group's reports with their symbolicated
// stacks. The Releases view shows crash-free rates per release and the groups
// new in each. Administrators only.
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
  const releases = values['view'] === 'releases';
  return (
    <>
      <div className="seg" role="group" aria-label={t('Crashes')}>
        <button
          aria-pressed={!releases}
          className={releases ? '' : 'on'}
          onClick={() => set({ view: undefined })}
        >
          {t('Groups')}
        </button>
        <button
          aria-pressed={releases}
          className={releases ? 'on' : ''}
          onClick={() => set({ view: 'releases' })}
        >
          {t('Releases')}
        </button>
      </div>
      {releases ? <Releases /> : <Groups />}
    </>
  );
}

const STATES: CrashGroupState[] = ['new', 'triaged', 'resolved'];
function stateLabel(state: CrashGroupState): string {
  return state === 'new' ? t('New') : state === 'triaged' ? t('Triaged') : t('Resolved');
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
  const [state, setState] = useFilterDraft(values['state'] ?? '');
  const query = Object.fromEntries(
    ['version', 'platform', 'distribution', 'state', 'cursor'].flatMap((key) =>
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
          set({
            ...Object.fromEntries(FILTERS.map((f) => [f.key, draft[f.key]?.trim()])),
            state: state || undefined,
          });
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
        <select aria-label={t('State')} value={state} onChange={(e) => setState(e.target.value)}>
          <option value="">{t('All')}</option>
          {STATES.map((value) => (
            <option key={value} value={value}>
              {stateLabel(value)}
            </option>
          ))}
        </select>
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

function Group({ group: initial }: { group: AdminCrashGroup }) {
  const { set } = useAdminFilters();
  const [group, setGroup] = useState(initial);
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
        <dt>{t('State')}</dt>
        <dd>
          {stateLabel(group.state)}
          {group.fixedIn &&
            (group.state === 'resolved' ? (
              <>
                {' · '}
                {t('Fixed in')} <code>{group.fixedIn}</code>
              </>
            ) : (
              <>
                {' · '}
                {t('Reopened after the fix in {value0}', { value0: group.fixedIn })}
              </>
            ))}
        </dd>
        {group.githubIssue && (
          <>
            <dt>{t('GitHub issue')}</dt>
            <dd>
              <a href={group.githubIssue.url} rel="noreferrer" target="_blank">
                {`#${group.githubIssue.number}`}
              </a>
            </dd>
          </>
        )}
      </dl>
      <Triage group={group} onSaved={setGroup} />
      <button onClick={() => set({ group: group.id })}>{t('Show reports')}</button>
    </article>
  );
}

/** Sets a group's triage state; resolving names the release with the fix. */
function Triage({
  group,
  onSaved,
}: {
  group: AdminCrashGroup;
  onSaved: (group: AdminCrashGroup) => void;
}) {
  const [state, setState] = useState<CrashGroupState>(group.state);
  const [fixedIn, setFixedIn] = useState(group.fixedIn ?? '');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  async function save() {
    setBusy(true);
    setError('');
    try {
      const saved = await api.setCrashGroupState(group.id, {
        state,
        ...(state === 'resolved' ? { fixedIn: fixedIn.trim() } : {}),
      });
      onSaved({
        ...group,
        state: saved.state,
        ...(saved.fixedIn ? { fixedIn: saved.fixedIn } : {}),
      });
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  }
  return (
    <div className="toolbar">
      <select
        aria-label={t('State')}
        value={state}
        onChange={(e) => setState(e.target.value as CrashGroupState)}
      >
        {STATES.map((value) => (
          <option key={value} value={value}>
            {stateLabel(value)}
          </option>
        ))}
      </select>
      {state === 'resolved' && (
        <input
          aria-label={t('Fixed in')}
          placeholder={t('Fixed in')}
          value={fixedIn}
          onChange={(e) => setFixedIn(e.target.value)}
        />
      )}
      <button
        disabled={busy || (state === 'resolved' && !fixedIn.trim())}
        onClick={() => void save()}
      >
        {t('Save')}
      </button>
      {error && <p role="alert">{displayMessage(error)}</p>}
    </div>
  );
}

function percent(value: number | undefined): string {
  return value === undefined
    ? '–'
    : formatNumber(value, { style: 'percent', minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

function Releases() {
  const { values } = useAdminFilters();
  const query = values['cursor'] ? { cursor: values['cursor'] } : {};
  const load = useLoad((signal) => api.adminCrashReleases(query, signal), [JSON.stringify(query)]);
  return (
    <Loaded load={load}>
      {(page) => (
        <>
          {page.items.length === 0 && <p>{t('No releases yet.')}</p>}
          {page.items.map((release) => (
            <Release key={release.version} release={release} />
          ))}
          <PageControls nextCursor={page.nextCursor} />
        </>
      )}
    </Loaded>
  );
}

function Rates({ stats }: { stats: CrashFreeStats }) {
  return (
    <>
      <dt>{t('Crash-free sessions')}</dt>
      <dd>{percent(stats.crashFreeSessions)}</dd>
      <dt>{t('Crash-free installs')}</dt>
      <dd>{percent(stats.crashFreeInstalls)}</dd>
      <dt>{t('Sessions')}</dt>
      <dd>{formatNumber(stats.sessions)}</dd>
    </>
  );
}

function Release({ release }: { release: AdminCrashRelease }) {
  const { set } = useAdminFilters();
  return (
    <article className="card" aria-labelledby={`release-${release.version}`}>
      <h3 id={`release-${release.version}`}>
        <code>{release.version}</code>
      </h3>
      <dl>
        <Rates stats={release.overall} />
        <dt>{t('Crash reports')}</dt>
        <dd>{formatNumber(release.reports)}</dd>
        <dt>{t('Affected installs')}</dt>
        <dd>{formatNumber(release.affectedInstalls)}</dd>
      </dl>
      {release.breakdown.length > 0 && (
        <table>
          <thead>
            <tr>
              <th>{t('Platform')}</th>
              <th>{t('Channel')}</th>
              <th>{t('Sessions')}</th>
              <th>{t('Crash-free sessions')}</th>
              <th>{t('Crash-free installs')}</th>
            </tr>
          </thead>
          <tbody>
            {release.breakdown.map((row) => (
              <tr key={`${row.platform}-${row.distribution}`}>
                <td>
                  <code>{row.platform}</code>
                </td>
                <td>
                  <code>{row.distribution}</code>
                </td>
                <td>{formatNumber(row.stats.sessions)}</td>
                <td>{percent(row.stats.crashFreeSessions)}</td>
                <td>{percent(row.stats.crashFreeInstalls)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      {release.newGroups.length > 0 && (
        <>
          <h4>{t('New in this release')}</h4>
          <ul>
            {release.newGroups.map((group) => (
              <li key={group.id}>
                <button onClick={() => set({ view: undefined, group: group.id })}>
                  <code style={{ overflowWrap: 'anywhere' }}>{group.signature}</code>
                </button>
                {' · '}
                {stateLabel(group.state)}
                {' · '}
                {t('Installs')} {formatNumber(group.installs)}
              </li>
            ))}
          </ul>
        </>
      )}
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
