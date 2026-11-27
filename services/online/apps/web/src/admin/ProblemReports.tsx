import type { AdminCrashReport } from '@glob2/protocol';
import { formatDate, formatNumber, t, useLocale } from '../i18n.tsx';
import { api } from '../api.ts';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { PageControls, useAdminFilters } from './filters.tsx';

// Problem reports sent from the game (docs/multiplayer/problem-reports.md).
// Administrators only: bundles hold a player's log, save and replay, and every
// download is written to the audit log.
export function ProblemReports() {
  useLocale();
  const { values } = useAdminFilters();
  const query = { ...values, kind: 'problem' };
  const load = useLoad((signal) => api.adminCrashReports(query, signal), [JSON.stringify(query)]);
  return (
    <Loaded load={load}>
      {(page) => (
        <>
          {page.items.length === 0 && <p>{t('No problem reports yet.')}</p>}
          {page.items.map((report) => (
            <Report key={report.id} report={report} />
          ))}
          <PageControls nextCursor={page.nextCursor} />
        </>
      )}
    </Loaded>
  );
}

const kilobytes = (bytes: number) =>
  formatNumber(Math.max(1, Math.round(bytes / 1024)), { style: 'unit', unit: 'kilobyte' });

const STATUS = { none: '', pending: 'Pending', done: 'Completed', failed: 'Failed' } as const;

export function Report({ report }: { report: AdminCrashReport }) {
  const base = `/api/v1/admin/crash-reports/${report.id}`;
  return (
    <article className="card" aria-labelledby={`problem-${report.id}`}>
      <h3 id={`problem-${report.id}`}>
        <code>{report.id}</code>{' '}
        <time dateTime={report.receivedAt}>
          {formatDate(report.receivedAt, { dateStyle: 'medium', timeStyle: 'short' })}
        </time>
      </h3>
      <dl>
        <dt>{t('Version')}</dt>
        <dd>
          <code>{[report.version, report.versionMinor, report.simRevision].join(' · ')}</code>
        </dd>
        <dt>{t('Platform')}</dt>
        <dd>
          <code>{[report.platform, report.distribution, report.os].join(' · ')}</code>
        </dd>
        <dt>{t('Device')}</dt>
        <dd>
          <code>{[report.deviceClass, report.deviceTier].join(' · ')}</code>
        </dd>
        {report.status !== 'none' && (
          <>
            <dt>{t('Status')}</dt>
            <dd>
              {t(STATUS[report.status])}
              {report.reason && (
                <>
                  {' · '}
                  <code>{report.reason}</code>
                </>
              )}
            </dd>
          </>
        )}
        <dt>{t('Description')}</dt>
        <dd style={{ whiteSpace: 'pre-wrap' }}>{report.description || t('No description.')}</dd>
        <dt>{t('Files')}</dt>
        <dd>
          {report.bundleAvailable ? (
            <ul>
              {report.attachments.map((file) => (
                <li key={file.name}>
                  <a href={`${base}/attachments/${encodeURIComponent(file.name)}`}>{file.name}</a> (
                  {kilobytes(file.size)})
                </li>
              ))}
              <li>
                <a href={`${base}/bundle`}>{t('Download bundle')}</a> ({kilobytes(report.size)})
              </li>
            </ul>
          ) : (
            t('Bundle deleted after 90 days.')
          )}
        </dd>
      </dl>
    </article>
  );
}
