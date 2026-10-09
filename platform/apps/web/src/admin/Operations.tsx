import { useState } from 'react';
import type { AdminOperation, AdminOperations, AdminOperationDetail } from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad } from '../state.tsx';
import { Loaded } from '../components/common.tsx';
import { PageControls, useAdminFilters } from './filters.tsx';
import { dateTime } from '../format.ts';
import { Link } from '../router.tsx';
import { productName } from './presentation.ts';

function RecoveryDetails({ details }: { details: AdminOperationDetail }) {
  return (
    <section aria-label={`Recovery evidence for ${details.id}`}>
      <p>
        Status: {details.status} · Created {dateTime(details.createdAt)}
        {details.completedAt ? ' · Completed ' + dateTime(details.completedAt) : ''}
      </p>
      <p>
        Reserved: {details.reserved} product credits
        {details.charged === null ? '' : ' · Charged: ' + details.charged}
      </p>
      <p>{details.creditConsequence}</p>
      {details.usage && (
        <p>
          Recorded usage: {details.usage.input} input tokens · {details.usage.cached} cached input ·{' '}
          {details.usage.cacheWrite ?? 0} cache write · {details.usage.output} output tokens
        </p>
      )}
      {details.attempts.length === 0 ? (
        <p>No attempt usage evidence is available.</p>
      ) : (
        <div
          tabIndex={0}
          role="region"
          aria-label={`Provider attempt evidence for ${details.id}`}
          style={{ overflowX: 'auto' }}
        >
          <table>
            <caption>
              Most recent provider attempts; unavailable usage is not zero. These numbers need
              verification against provider evidence.
            </caption>
            <thead>
              <tr>
                <th>Created</th>
                <th>Model / stage</th>
                <th>Status</th>
                <th>Input tokens</th>
                <th>Cached input</th>
                <th>Cache write tokens</th>
                <th>Output tokens</th>
              </tr>
            </thead>
            <tbody>
              {details.attempts.map((attempt, i) => (
                <tr key={i}>
                  <td>{dateTime(attempt.createdAt)}</td>
                  <th>
                    {attempt.model} / {attempt.stage}
                  </th>
                  <td>{attempt.status}</td>
                  <td>{attempt.usage?.input ?? 'Unavailable'}</td>
                  <td>{attempt.usage?.cached ?? 'Unavailable'}</td>
                  <td>{attempt.usage ? (attempt.usage.cacheWrite ?? 0) : 'Unavailable'}</td>
                  <td>{attempt.usage?.output ?? 'Unavailable'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}
function Recovery({ row, reload }: { row: AdminOperation; reload: () => void }) {
  const [reason, setReason] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('');
  // Blank fields require an explicit usage decision, including a genuine zero.
  const [usage, setUsage] = useState({ input: '', cachedInput: '', cacheWrite: '0', output: '' });
  const [verified, setVerified] = useState(false);
  const [details, setDetails] = useState<AdminOperationDetail>();
  const metered = row.product === 'hive' || ['aiStudio', 'generatorStudio'].includes(row.product);
  const validUsage =
    Object.values(usage).every(
      (value) => /^\d+$/.test(value) && Number.isSafeInteger(Number(value)),
    ) && Number(usage.cachedInput) + Number(usage.cacheWrite) <= Number(usage.input);
  const ready =
    details?.status === 'uncertain' && (metered ? validUsage && verified : true) && !!reason.trim();
  async function inspect() {
    setBusy(true);
    setError('');
    try {
      setDetails(
        await request<AdminOperationDetail>(
          'GET',
          `/api/v1/admin/operations/${row.product}/${row.id}`,
        ),
      );
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }
  async function recover() {
    if (
      busy ||
      !ready ||
      !window.confirm(
        metered
          ? `Reconcile ${usage.input} input (${usage.cachedInput} cached; ${usage.cacheWrite} cache write) and ${usage.output} output tokens? Verified usage is charged up to the ${row.reserved}-credit reservation and the remainder is released.`
          : `Mark this generation failed and return ${row.reserved} reserved product credit(s)? Confirm that its delivery cannot be recovered.`,
      )
    )
      return;
    setBusy(true);
    setError('');
    const path =
      row.product === 'hive'
        ? `/api/v1/admin/hive/calls/${row.id}/reconcile`
        : ['aiStudio', 'generatorStudio'].includes(row.product)
          ? `/api/v1/${row.product === 'generatorStudio' ? 'generator-studio' : 'ai-studio'}/reconcile`
          : `/api/v1/admin/${({ maps: 'map-studio', music: 'music-studio', terrain: 'terrain-studio', buildings: 'ai-building-studio' } as Record<string, string>)[row.product]}/requests/${row.id}/fail`;
    try {
      await request('POST', path, {
        body: metered
          ? {
              ...(['aiStudio', 'generatorStudio'].includes(row.product)
                ? { requestId: row.id }
                : {}),
              usage: {
                input: Number(usage.input),
                cachedInput: Number(usage.cachedInput),
                output: Number(usage.output),
                ...(Number(usage.cacheWrite) > 0 ? { cacheWrite: Number(usage.cacheWrite) } : {}),
              },
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
    <article className="card" style={{ overflowWrap: 'anywhere' }}>
      <h3>
        {productName(row.product)} · {row.kind}
      </h3>
      <p>
        {row.id} · {row.status} · {dateTime(row.createdAt)}
      </p>
      {row.error && <p>{row.error}</p>}
      {row.accountId && (
        <p>
          <Link to={`/admin/accounts?q=${row.accountId}`}>Inspect requesting account</Link>
        </p>
      )}
      <p>Reserved: {row.reserved} product credits</p>
      {row.product === 'engine' ? (
        <p>
          <Link to="/admin/matches?verification=failed">Inspect match verification</Link> or inspect
          the reported library item’s validation.
        </p>
      ) : (
        <>
          <button disabled={busy} onClick={() => void inspect()}>
            Inspect recovery details
          </button>
          {details && <RecoveryDetails details={details} />}
          <p>
            {metered
              ? 'Enter usage verified against provider evidence. Input totals include cache creation/write tokens. Unknown usage must not be entered as zero.'
              : 'Returning the reservation marks this generation failed. Inspect its details and confirm delivery cannot be recovered first.'}
          </p>
          <fieldset disabled={busy}>
            <label className="field">
              Reason and evidence
              <textarea
                value={reason}
                maxLength={2000}
                onChange={(e) => setReason(e.target.value)}
              />
            </label>
            {metered && (
              <>
                {(['input', 'cachedInput', 'cacheWrite', 'output'] as const).map((key) => (
                  <label className="field" key={key}>
                    {
                      {
                        input: 'Input tokens (including cached)',
                        cachedInput: 'Cached input tokens',
                        cacheWrite: 'Cache creation/write tokens',
                        output: 'Output tokens',
                      }[key]
                    }
                    <input
                      type="number"
                      min={0}
                      step={1}
                      value={usage[key]}
                      onChange={(e) => {
                        setUsage({ ...usage, [key]: e.target.value });
                        setVerified(false);
                      }}
                    />
                  </label>
                ))}
                {details?.usage && (
                  <button
                    onClick={() => {
                      const measured = details.usage;
                      if (measured) {
                        setUsage({
                          input: String(measured.input),
                          cachedInput: String(measured.cached),
                          cacheWrite: String(measured.cacheWrite ?? 0),
                          output: String(measured.output),
                        });
                        setVerified(false);
                      }
                    }}
                  >
                    Copy recorded usage
                  </button>
                )}
                <label>
                  <input
                    type="checkbox"
                    checked={verified}
                    onChange={(e) => setVerified(e.target.checked)}
                  />{' '}
                  I checked these token counts against provider evidence, including any explicitly
                  entered zeros.
                </label>
              </>
            )}
            <button disabled={!ready} onClick={() => void recover()}>
              {metered ? 'Reconcile verified usage' : 'Return reserved credit'}
            </button>
          </fieldset>
        </>
      )}
      {busy && <p role="status">Working…</p>}
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
          {[
            'maps',
            'music',
            'terrain',
            'buildings',
            'aiStudio',
            'generatorStudio',
            'hive',
            'engine',
          ].map((p) => (
            <option key={p} value={p}>
              {productName(p)}
            </option>
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
                {productName(r.product)}: {r.reserved}
              </p>
            ))}
            <h3>Engine agents</h3>
            {data.agents.length === 0 && <p>No engine agents are registered.</p>}
            {data.agents.map((a) => (
              <p key={a.id}>
                {a.id} · last seen {dateTime(a.lastSeenAt)}
                {Date.now() - Date.parse(a.lastSeenAt) > 90000 ? ' · stale' : ''}
              </p>
            ))}
            <h3>Worker leases</h3>
            {data.workers.length === 0 && <p>No worker leases are recorded.</p>}
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
