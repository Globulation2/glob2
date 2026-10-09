import { displayMessage } from '../i18n.tsx';
import { translateError } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useState } from 'react';
import type { SkinReportInfo, SkinReportList } from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad } from '../state.tsx';
import { dateTime } from '../format.ts';

function Report({ report, reload }: { report: SkinReportInfo; reload: () => void }) {
  useLocale();
  const [reason, setReason] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  async function act(action: 'dismissed' | 'disabled' | 'restore') {
    setBusy(true);
    setError('');
    try {
      if (action === 'restore')
        await request('POST', `/api/v1/admin/skins/${report.skinId}/moderation`, {
          body: { disabled: false, reason },
        });
      else
        await request('POST', `/api/v1/admin/skin-reports/${report.id}/resolve`, {
          body: { resolution: action, reason },
        });
      reload();
    } catch (e) {
      setError(e instanceof Error ? e.message : t('Could not update report.'));
    } finally {
      setBusy(false);
    }
  }
  return (
    <article className="card" aria-label={t('Report for {value0}', { value0: report.name })}>
      <h3>{report.name}</h3>
      <details>
        <summary>{t('View reported paint')}</summary>
        <img
          width={256}
          height={256}
          src={`/api/v1/admin/skins/versions/${report.versionId}/texture`}
          alt={t('Reported skin texture')}
        />
      </details>
      <p>
        <RichMessage
          source={'{slot0} · {slot1}'}
          slots={{ slot0: report.reporterName, slot1: dateTime(report.createdAt) }}
        />
      </p>
      <p style={{ whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>{report.reason}</p>
      {report.disabledAt && <p>{t('Skin disabled')}</p>}
      {report.resolution && (
        <p>
          <RichMessage
            source={'{slot0}: {slot1}'}
            slots={{
              slot0: report.resolution === 'disabled' ? t('Resolved by disabling') : t('Dismissed'),
              slot1: report.resolutionReason,
            }}
          />
        </p>
      )}
      {(!report.resolution || report.disabledAt) && (
        <fieldset disabled={busy}>
          <label>
            {t('Moderation reason')}{' '}
            <textarea maxLength={1000} value={reason} onChange={(e) => setReason(e.target.value)} />
          </label>
          {!report.resolution && (
            <>
              <button disabled={!reason.trim()} onClick={() => void act('dismissed')}>
                {t('Dismiss report')}
              </button>
              <button disabled={!reason.trim()} onClick={() => void act('disabled')}>
                {t('Disable skin and resolve')}
              </button>
            </>
          )}
          {report.disabledAt && (
            <button disabled={!reason.trim()} onClick={() => void act('restore')}>
              {t('Restore skin')}
            </button>
          )}
        </fieldset>
      )}
      {error && <p role="alert">{displayMessage(error)}</p>}
    </article>
  );
}
export function SkinReports() {
  useLocale();
  const [status, setStatus] = useState('open');
  const [cursor, setCursor] = useState<string>();
  const load = useLoad(
    (signal) =>
      request<SkinReportList>('GET', '/api/v1/admin/skin-reports', {
        query: { status, cursor },
        signal,
      }),
    [status, cursor],
  );
  return (
    <section aria-label={t('Skin reports')}>
      <label>
        {t('Skin report status')}{' '}
        <select
          value={status}
          onChange={(e) => {
            setStatus(e.target.value);
            setCursor(undefined);
          }}
        >
          <option value="open">{t('Open')}</option>
          <option value="closed">{t('Closed')}</option>
        </select>
      </label>
      {load.status === 'loading' && <p>{t('Loading reports…')}</p>}
      {load.status === 'error' && <p role="alert">{translateError(load.error)}</p>}
      {load.status === 'ready' && (
        <>
          {!load.data.items.length && <p>{t('No skin reports.')}</p>}
          {load.data.items.map((report) => (
            <Report key={report.id} report={report} reload={load.reload} />
          ))}
          {load.data.nextCursor && (
            <button onClick={() => setCursor(load.data.nextCursor)}>{t('Older reports')}</button>
          )}
        </>
      )}
      {cursor && <button onClick={() => setCursor(undefined)}>{t('Newest reports')}</button>}
    </section>
  );
}
