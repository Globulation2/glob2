import { useState } from 'react';
import type { SkinReportInfo, SkinReportList } from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad } from '../state.tsx';
import { dateTime } from '../format.ts';

function Report({ report, reload }: { report: SkinReportInfo; reload: () => void }) {
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
      setError(e instanceof Error ? e.message : 'Could not update report.');
    } finally {
      setBusy(false);
    }
  }
  return (
    <article className="card" aria-label={`Report for ${report.name}`}>
      <h3>{report.name}</h3>
      <details>
        <summary>View reported paint</summary>
        <img
          width={256}
          height={256}
          src={`/api/v1/admin/skins/versions/${report.versionId}/texture`}
          alt="Reported skin texture"
        />
      </details>
      <p>
        {report.reporterName} · {dateTime(report.createdAt)}
      </p>
      <p style={{ whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>{report.reason}</p>
      {report.disabledAt && <p>Skin disabled</p>}
      {report.resolution && (
        <p>
          {report.resolution === 'disabled' ? 'Resolved by disabling' : 'Dismissed'}:{' '}
          {report.resolutionReason}
        </p>
      )}
      {(!report.resolution || report.disabledAt) && (
        <fieldset disabled={busy}>
          <label>
            Moderation reason{' '}
            <textarea maxLength={1000} value={reason} onChange={(e) => setReason(e.target.value)} />
          </label>
          {!report.resolution && (
            <>
              <button disabled={!reason.trim()} onClick={() => void act('dismissed')}>
                Dismiss report
              </button>
              <button disabled={!reason.trim()} onClick={() => void act('disabled')}>
                Disable skin and resolve
              </button>
            </>
          )}
          {report.disabledAt && (
            <button disabled={!reason.trim()} onClick={() => void act('restore')}>
              Restore skin
            </button>
          )}
        </fieldset>
      )}
      {error && <p role="alert">{error}</p>}
    </article>
  );
}
export function SkinReports() {
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
    <section aria-label="Skin reports">
      <label>
        Skin report status{' '}
        <select
          value={status}
          onChange={(e) => {
            setStatus(e.target.value);
            setCursor(undefined);
          }}
        >
          <option value="open">Open</option>
          <option value="closed">Closed</option>
        </select>
      </label>
      {load.status === 'loading' && <p>Loading reports…</p>}
      {load.status === 'error' && <p role="alert">{load.error.message}</p>}
      {load.status === 'ready' && (
        <>
          {!load.data.items.length && <p>No skin reports.</p>}
          {load.data.items.map((report) => (
            <Report key={report.id} report={report} reload={load.reload} />
          ))}
          {load.data.nextCursor && (
            <button onClick={() => setCursor(load.data.nextCursor)}>Older reports</button>
          )}
        </>
      )}
      {cursor && <button onClick={() => setCursor(undefined)}>Newest reports</button>}
    </section>
  );
}
