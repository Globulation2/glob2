import { statusLabel } from '../i18n.tsx';
import { displayMessage } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
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
  useLocale();
  return (
    <label>
      {t('Library')}{' '}
      <select value={value} onChange={(e) => onChange(e.target.value)}>
        <option value="">{t('All libraries')}</option>
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
  useLocale();
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
        <RichMessage
          source={'{slot0} · {slot1} · {slot2}{slot3}'}
          slots={{
            slot0: report.reporterName,
            slot1: dateTime(report.createdAt),
            slot2: statusLabel(report.status),
            slot3: report.hidden ? t(' · Content hidden') : '',
          }}
        />
      </p>
      <p style={{ whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {report.reason}
        {report.details ? ' — ' + report.details : ''}
      </p>
      {report.previewHref && (
        <img src={report.previewHref} width={256} height={256} alt={t('Reported skin texture')} />
      )}
      {report.resolution && (
        <p>
          <RichMessage
            source={'Resolution: {slot0}{slot1}'}
            slots={{
              slot0: statusLabel(report.resolution),
              slot1: report.resolvedAt ? ' · ' + dateTime(report.resolvedAt) : '',
            }}
          />
        </p>
      )}
      {report.status === 'open' && (
        <fieldset disabled={busy}>
          <label>
            {t('Moderation reason')}{' '}
            <textarea
              value={reason}
              maxLength={report.library === 'skins' ? 1000 : 2000}
              onChange={(e) => setReason(e.target.value)}
            />
          </label>
          <div className="toolbar">
            <button disabled={!reason.trim()} onClick={() => void resolve('resolved', true)}>
              <RichMessage
                source={'{slot0} and resolve'}
                slots={{ slot0: report.library === 'skins' ? t('Disable') : t('Hide') }}
              />
            </button>
            {report.library !== 'skins' && (
              <button disabled={!reason.trim()} onClick={() => void resolve('resolved')}>
                {t('Resolve')}
              </button>
            )}
            <button disabled={!reason.trim()} onClick={() => void resolve('dismissed')}>
              {t('Dismiss')}
            </button>
          </div>
        </fieldset>
      )}
      {error && <p role="alert">{displayMessage(error)}</p>}
    </article>
  );
}
export function UnifiedReports({ library }: { library?: string }) {
  useLocale();
  const { values, set } = useAdminFilters({ status: 'open', library: library ?? '' });
  const load = useLoad(
    (signal) => request<AdminReportList>('GET', '/api/v1/admin/reports', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>{t('Reports')}</h2>
      <div className="toolbar">
        <LibraryFilter value={values['library'] ?? ''} onChange={(v) => set({ library: v })} />
        <label>
          {t('Status')}{' '}
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
              {LIBRARIES.map((l) =>
                t('{value0}: {count} open', { value0: libraryName(l), count: page.counts[l] ?? 0 }),
              ).join(' · ')}
            </p>
            {page.items.length === 0 ? (
              <Empty>{t('No reports.')}</Empty>
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
  useLocale();
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
        <RichMessage
          source={'{slot0} · {slot1}{slot2}'}
          slots={{
            slot0: content.hidden ? t('Hidden') : t('Not hidden'),
            slot1: dateTime(content.createdAt),
            slot2:
              content.downloads !== null
                ? t(' · {count} recorded downloads', { count: content.downloads })
                : '',
          }}
        />
      </p>
      {content.reason && <p>{content.reason}</p>}
      {content.previewHref && (
        <img src={content.previewHref} width={256} height={256} alt={t('Skin texture')} />
      )}
      <label>
        {t('Moderation reason')}{' '}
        <input
          value={reason}
          maxLength={content.library === 'skins' ? 1000 : 2000}
          onChange={(e) => setReason(e.target.value)}
        />
      </label>
      <button disabled={busy || !reason.trim()} onClick={() => void act()}>
        {content.hidden ? t('Restore') : t('Hide')}
      </button>
      {error && <p role="alert">{displayMessage(error)}</p>}
    </article>
  );
}
export function Content() {
  useLocale();
  const { values, set } = useAdminFilters({ hidden: 'true' }),
    [q, setQ] = useFilterDraft(values['q'] ?? '');
  const load = useLoad(
    (signal) =>
      request<AdminContentList>('GET', '/api/v1/admin/content', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>{t('Content')}</h2>
      <p>
        {t(
          'Restoring content removes its moderation restriction; each library’s publication rules still apply.',
        )}
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
          {t('Moderation visibility')}{' '}
          <select value={values['hidden']} onChange={(e) => set({ hidden: e.target.value })}>
            <option value="true">{t('Hidden')}</option>
            <option value="false">{t('Not hidden')}</option>
            <option value="all">{t('All')}</option>
          </select>
        </label>
        <input
          aria-label={t('Find content')}
          placeholder={t('Name or content ID')}
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <button>{t('Search')}</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            {page.items.length === 0 ? (
              <Empty>{t('No content found.')}</Empty>
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
  useLocale();
  const { values, set } = useAdminFilters(),
    [draft, setDraft] = useFilterDraft(values);
  const load = useLoad(
    (signal) => request<AdminAuditList>('GET', '/api/v1/admin/audit', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>{t('Audit history')}</h2>
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
                  actor: t('Actor account ID'),
                  action: t('Action'),
                  target: t('Target ID or type'),
                  from: t('From (UTC)'),
                  to: t('Through (UTC)'),
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
        <button>{t('Filter')}</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            {page.items.length === 0 ? (
              <Empty>{t('No actions found.')}</Empty>
            ) : (
              page.items.map((row) => (
                <article key={row.id} className="card">
                  <p>
                    <RichMessage
                      source={'{slot0} · {slot1} · {slot2}'}
                      slots={{
                        slot0: dateTime(row.createdAt),
                        slot1: row.actorName,
                        slot2: row.action,
                      }}
                    />
                  </p>
                  <p>
                    <RichMessage
                      source={'{slot0}: {slot1}'}
                      slots={{ slot0: row.targetType, slot1: row.targetId }}
                    />
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
