import { useState } from 'react';
import type { AdminOperation, AdminOperations } from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad } from '../state.tsx';
import { Loaded } from '../components/common.tsx';
import { PageControls, useAdminFilters } from './Moderation.tsx';
import { dateTime } from '../format.ts';

function Recovery({ row, reload }: { row: AdminOperation; reload: () => void }) {
  const [reason, setReason] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('');
  const [usage, setUsage] = useState({ input: 0, cachedInput: 0, output: 0 }),
    [details, setDetails] = useState<unknown>();
  const metered = row.product === 'hive' || row.product === 'aiStudio';
  async function recover() {
    if (
      !window.confirm(
        metered
          ? 'Charge the verified usage and release the remaining reservation?'
          : 'Confirm this generation cannot be recovered and return its reserved credit?',
      )
    )
      return;
    setBusy(true);
    setError('');
    const path =
      row.product === 'hive'
        ? `/api/v1/admin/hive/calls/${row.id}/reconcile`
        : row.product === 'aiStudio'
          ? '/api/v1/ai-studio/reconcile'
          : `/api/v1/admin/${({ maps: 'map-studio', music: 'music-studio', terrain: 'terrain-studio', buildings: 'ai-building-studio' } as Record<string, string>)[row.product]}/requests/${row.id}/fail`;
    try {
      await request('POST', path, {
        body: metered
          ? {
              ...(row.product === 'aiStudio' ? { requestId: row.id } : {}),
              usage,
              evidence: reason,
            }
          : { reason },
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
        {row.product} · {row.kind}
      </h3>
      <p>
        {row.id} · {row.status} · {dateTime(row.createdAt)}
      </p>
      {row.error && <p>{row.error}</p>}
      <p>Reserved: {row.reserved} product credits</p>
      {row.product !== 'engine' && (
        <>
          <button
            onClick={() =>
              void request('GET', `/api/v1/admin/operations/${row.product}/${row.id}`)
                .then(setDetails)
                .catch((e) => setError(String(e)))
            }
          >
            Inspect recovery details
          </button>
          {details !== undefined && (
            <pre style={{ whiteSpace: 'pre-wrap' }}>{JSON.stringify(details, null, 2)}</pre>
          )}
          <p>
            {metered
              ? 'Measured usage is charged up to the reservation; the rest is released. Enter usage verified against provider evidence.'
              : 'Returning the reservation marks this generation failed. Confirm the provider outcome cannot be recovered first.'}
          </p>
          <label>
            Reason and evidence{' '}
            <textarea value={reason} maxLength={2000} onChange={(e) => setReason(e.target.value)} />
          </label>
          {metered &&
            Object.entries(usage).map(([key, value]) => (
              <label key={key}>
                {key} tokens{' '}
                <input
                  type="number"
                  min={0}
                  value={value}
                  onChange={(e) => setUsage({ ...usage, [key]: Number(e.target.value) })}
                />
              </label>
            ))}
          <button disabled={busy || !reason.trim()} onClick={() => void recover()}>
            {metered ? 'Reconcile usage' : 'Return reserved credit'}
          </button>
        </>
      )}
      {error && <p role="alert">{error}</p>}
    </article>
  );
}
export function Operations() {
  const { values, set } = useAdminFilters();
  const load = useLoad(
    (signal) =>
      request<AdminOperations>('GET', '/api/v1/admin/operations', { signal, query: values }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>Operations</h2>
      <label>
        Product{' '}
        <select value={values['product'] ?? ''} onChange={(e) => set({ product: e.target.value })}>
          <option value="">All</option>
          {['maps', 'music', 'terrain', 'buildings', 'aiStudio', 'hive', 'engine'].map((p) => (
            <option key={p}>{p}</option>
          ))}
        </select>
      </label>
      <button onClick={load.reload}>Refresh</button>
      <Loaded load={load}>
        {(data) => (
          <>
            <p>
              Oldest queued engine job:{' '}
              {data.queueAgeSeconds === null
                ? 'none'
                : `${Math.round(data.queueAgeSeconds)} seconds`}
            </p>
            <h3>Reserved product credits</h3>
            {data.reservedCredits.map((r) => (
              <p key={r.product}>
                {r.product}: {r.reserved}
              </p>
            ))}
            <h3>Engine agents</h3>
            {data.agents.map((a) => (
              <p key={a.id}>
                {a.id} · last seen {dateTime(a.lastSeenAt)}
                {Date.now() - Date.parse(a.lastSeenAt) > 90000 ? ' · stale' : ''}
              </p>
            ))}
            <h3>Worker leases</h3>
            {data.workers.map((w) => (
              <p key={w.name}>
                {w.name} · {w.holder} · last renewed {dateTime(w.renewedAt)}
                {Date.now() - Date.parse(w.renewedAt) > 90000 ? ' · stale' : ''}
              </p>
            ))}
            <h3>Requests needing attention</h3>
            <p>Uncertain requests and queued/failed engine jobs, oldest first.</p>
            {data.items.length === 0 ? (
              <p>No requests need attention.</p>
            ) : (
              data.items.map((r) => (
                <Recovery key={r.product + r.id} row={r} reload={load.reload} />
              ))
            )}
            <PageControls nextCursor={data.nextCursor} />
          </>
        )}
      </Loaded>
    </section>
  );
}
