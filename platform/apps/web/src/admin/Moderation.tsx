import { useState } from 'react';
import type {
  AdminReport,
  AdminReportList,
  AdminContent,
  AdminContentList,
  AdminAuditList,
} from '@glob2/protocol';
import { request } from '../api.ts';
import { Loaded, Empty } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { Link } from '../router.tsx';
import { dateTime } from '../format.ts';
import { PageControls, useAdminFilters, useFilterDraft } from './filters.tsx';
import { libraryName } from './presentation.ts';

const LIBRARIES = ['maps', 'ais', 'generators', 'buildings', 'sets', 'skins', 'music'];
function LibraryFilter({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  return (
    <label>
      Library{' '}
      <select value={value} onChange={(e) => onChange(e.target.value)}>
        <option value="">All libraries</option>
        {LIBRARIES.map((l) => (
          <option key={l} value={l}>
            {libraryName(l)}
          </option>
        ))}
      </select>
    </label>
  );
}
function ReportRow({ report, reload }: { report: AdminReport; reload: () => void }) {
  const [reason, setReason] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('');
  async function resolve(resolution: 'resolved' | 'dismissed', hide = false) {
    setBusy(true);
    setError('');
    try {
      await request('POST', `/api/v1/admin/reports/${report.library}/${report.id}/resolve`, {
        body: { resolution, hide, reason },
      });
      reload();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }
  return (
    <article className="card" data-testid="admin-report">
      <h3>
        <Link to={report.href}>{report.name}</Link>{' '}
        <span className="badge">{libraryName(report.library)}</span>
      </h3>
      <p>
        {report.reporterName} · {dateTime(report.createdAt)} · {report.status}
        {report.hidden ? ' · Content hidden' : ''}
      </p>
      <p style={{ whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {report.reason}
        {report.details ? ' — ' + report.details : ''}
      </p>
      {report.previewHref && (
        <img src={report.previewHref} width={256} height={256} alt="Reported skin texture" />
      )}
      {report.resolution && (
        <p>
          Resolution: {report.resolution}
          {report.resolvedAt ? ' · ' + dateTime(report.resolvedAt) : ''}
        </p>
      )}
      {report.status === 'open' && (
        <fieldset disabled={busy}>
          <label>
            Moderation reason{' '}
            <textarea
              value={reason}
              maxLength={report.library === 'skins' ? 1000 : 2000}
              onChange={(e) => setReason(e.target.value)}
            />
          </label>
          <div className="toolbar">
            <button disabled={!reason.trim()} onClick={() => void resolve('resolved', true)}>
              {report.library === 'skins' ? 'Disable' : 'Hide'} and resolve
            </button>
            {report.library !== 'skins' && (
              <button disabled={!reason.trim()} onClick={() => void resolve('resolved')}>
                Resolve
              </button>
            )}
            <button disabled={!reason.trim()} onClick={() => void resolve('dismissed')}>
              Dismiss
            </button>
          </div>
        </fieldset>
      )}
      {error && <p role="alert">{error}</p>}
    </article>
  );
}
export function UnifiedReports({ library }: { library?: string }) {
  const { values, set } = useAdminFilters({ status: 'open', library: library ?? '' });
  const load = useLoad(
    (signal) => request<AdminReportList>('GET', '/api/v1/admin/reports', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>Reports</h2>
      <div className="toolbar">
        <LibraryFilter value={values['library'] ?? ''} onChange={(v) => set({ library: v })} />
        <label>
          Status{' '}
          <select value={values['status']} onChange={(e) => set({ status: e.target.value })}>
            {['open', 'resolved', 'dismissed', 'all'].map((s) => (
              <option key={s}>{s}</option>
            ))}
          </select>
        </label>
      </div>
      <Loaded load={load}>
        {(page) => (
          <>
            <p>
              {LIBRARIES.map((l) => `${libraryName(l)}: ${page.counts[l] ?? 0} open`).join(' · ')}
            </p>
            {page.items.length === 0 ? (
              <Empty>No reports.</Empty>
            ) : (
              page.items.map((r) => (
                <ReportRow key={r.library + r.id} report={r} reload={load.reload} />
              ))
            )}
            <PageControls nextCursor={page.nextCursor} />
          </>
        )}
      </Loaded>
    </section>
  );
}
function ContentRow({ content, reload }: { content: AdminContent; reload: () => void }) {
  const [reason, setReason] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('');
  async function act() {
    setBusy(true);
    setError('');
    try {
      await request('POST', `/api/v1/admin/content/${content.library}/${content.id}/moderation`, {
        body: { hidden: !content.hidden, reason },
      });
      reload();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }
  return (
    <article className="card">
      <h3>
        <Link to={content.href}>{content.name}</Link>{' '}
        <span className="badge">{libraryName(content.library)}</span>
      </h3>
      <p>
        {content.hidden ? 'Hidden' : 'Not hidden'} · {dateTime(content.createdAt)}
        {content.downloads !== null ? ` · ${content.downloads} recorded downloads` : ''}
      </p>
      {content.reason && <p>{content.reason}</p>}
      {content.previewHref && (
        <img src={content.previewHref} width={256} height={256} alt="Skin texture" />
      )}
      <label>
        Moderation reason{' '}
        <input
          value={reason}
          maxLength={content.library === 'skins' ? 1000 : 2000}
          onChange={(e) => setReason(e.target.value)}
        />
      </label>
      <button disabled={busy || !reason.trim()} onClick={() => void act()}>
        {content.hidden ? 'Restore' : 'Hide'}
      </button>
      {error && <p role="alert">{error}</p>}
    </article>
  );
}
export function Content() {
  const { values, set } = useAdminFilters({ hidden: 'true' }),
    [q, setQ] = useFilterDraft(values['q'] ?? '');
  const load = useLoad(
    (signal) =>
      request<AdminContentList>('GET', '/api/v1/admin/content', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>Content</h2>
      <p>
        Restoring content removes its moderation restriction; each library’s publication rules still
        apply.
      </p>
      <form
        className="toolbar"
        onSubmit={(e) => {
          e.preventDefault();
          set({ q });
        }}
      >
        <LibraryFilter value={values['library'] ?? ''} onChange={(v) => set({ library: v })} />
        <label>
          Moderation visibility{' '}
          <select value={values['hidden']} onChange={(e) => set({ hidden: e.target.value })}>
            <option value="true">Hidden</option>
            <option value="false">Not hidden</option>
            <option value="all">All</option>
          </select>
        </label>
        <input
          aria-label="Find content"
          placeholder="Name or content ID"
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <button>Search</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            {page.items.length === 0 ? (
              <Empty>No content found.</Empty>
            ) : (
              page.items.map((c) => (
                <ContentRow key={c.library + c.id} content={c} reload={load.reload} />
              ))
            )}
            <PageControls nextCursor={page.nextCursor} />
          </>
        )}
      </Loaded>
    </section>
  );
}
export function Audit() {
  const { values, set } = useAdminFilters(),
    [draft, setDraft] = useFilterDraft(values);
  const load = useLoad(
    (signal) => request<AdminAuditList>('GET', '/api/v1/admin/audit', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>Audit history</h2>
      <form
        className="toolbar"
        onSubmit={(e) => {
          e.preventDefault();
          set({ ...draft, cursor: undefined });
        }}
      >
        {['actor', 'action', 'target', 'from', 'to'].map((key) => (
          <label key={key}>
            {
              (
                {
                  actor: 'Actor account ID',
                  action: 'Action',
                  target: 'Target ID or type',
                  from: 'From (UTC)',
                  to: 'Through (UTC)',
                } as Record<string, string>
              )[key]
            }
            <input
              type={key === 'from' || key === 'to' ? 'date' : 'text'}
              value={draft[key] ?? ''}
              onChange={(e) => setDraft({ ...draft, [key]: e.target.value })}
            />
          </label>
        ))}
        <button>Filter</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            {page.items.length === 0 ? (
              <Empty>No actions found.</Empty>
            ) : (
              page.items.map((row) => (
                <article key={row.id} className="card">
                  <p>
                    {dateTime(row.createdAt)} · {row.actorName} · {row.action}
                  </p>
                  <p>
                    {row.targetType}: {row.targetId}
                  </p>
                  <pre style={{ whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
                    {JSON.stringify(row.details, null, 2)}
                  </pre>
                </article>
              ))
            )}
            <PageControls nextCursor={page.nextCursor} />
          </>
        )}
      </Loaded>
    </section>
  );
}
