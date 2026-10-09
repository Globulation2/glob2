import { statusLabel } from '../i18n.tsx';
import { displayMessage } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
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
  useLocale();
  return (
    <section aria-label={t('Recovery evidence for {value0}', { value0: details.id })}>
      <p>
        <RichMessage
          source={'Status: {slot0} · Created {slot1}{slot2}'}
          slots={{
            slot0: statusLabel(details.status),
            slot1: dateTime(details.createdAt),
            slot2: details.completedAt ? t(' · Completed ') + dateTime(details.completedAt) : '',
          }}
        />
      </p>
      <p>
        <RichMessage
          source={'Reserved: {slot0} product credits{slot1}'}
          slots={{
            slot0: details.reserved,
            slot1: details.charged === null ? '' : t(' · Charged: ') + details.charged,
          }}
        />
      </p>
      <p>{details.creditConsequence}</p>
      {details.usage && (
        <p>
          <RichMessage
            source={
              'Recorded usage: {slot0} input tokens · {slot1} cached input · {slot2} cache write · {slot3} output tokens'
            }
            slots={{
              slot0: details.usage.input,
              slot1: details.usage.cached,
              slot2: details.usage.cacheWrite ?? 0,
              slot3: details.usage.output,
            }}
          />
        </p>
      )}
      {details.attempts.length === 0 ? (
        <p>{t('No attempt usage evidence is available.')}</p>
      ) : (
        <div
          tabIndex={0}
          role="region"
          aria-label={t('Provider attempt evidence for {value0}', { value0: details.id })}
          style={{ overflowX: 'auto' }}
        >
          <table>
            <caption>
              {t(
                'Most recent provider attempts; unavailable usage is not zero. These numbers need verification against provider evidence.',
              )}
            </caption>
            <thead>
              <tr>
                <th>{t('Created')}</th>
                <th>{t('Model / stage')}</th>
                <th>{t('Status')}</th>
                <th>{t('Input tokens')}</th>
                <th>{t('Cached input')}</th>
                <th>{t('Cache write tokens')}</th>
                <th>{t('Output tokens')}</th>
              </tr>
            </thead>
            <tbody>
              {details.attempts.map((attempt, i) => (
                <tr key={i}>
                  <td>{dateTime(attempt.createdAt)}</td>
                  <th>
                    <RichMessage
                      source={'{slot0} / {slot1}'}
                      slots={{ slot0: attempt.model, slot1: attempt.stage }}
                    />
                  </th>
                  <td>{statusLabel(attempt.status)}</td>
                  <td>{attempt.usage?.input ?? t('Unavailable')}</td>
                  <td>{attempt.usage?.cached ?? t('Unavailable')}</td>
                  <td>{attempt.usage ? (attempt.usage.cacheWrite ?? 0) : t('Unavailable')}</td>
                  <td>{attempt.usage?.output ?? t('Unavailable')}</td>
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
  useLocale();
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
          : t(
              'Mark this generation failed and return {value0} reserved product credit(s)? Confirm that its delivery cannot be recovered.',
              { value0: row.reserved },
            ),
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
        <RichMessage
          source={'{slot0} · {slot1}'}
          slots={{ slot0: productName(row.product), slot1: statusLabel(row.kind) }}
        />
      </h3>
      <p>
        <RichMessage
          source={'{slot0} · {slot1} · {slot2}'}
          slots={{ slot0: row.id, slot1: statusLabel(row.status), slot2: dateTime(row.createdAt) }}
        />
      </p>
      {row.error && <p>{row.error}</p>}
      {row.accountId && (
        <p>
          <Link to={`/admin/accounts?q=${row.accountId}`}>{t('Inspect requesting account')}</Link>
        </p>
      )}
      <p>
        <RichMessage source={'Reserved: {slot0} product credits'} slots={{ slot0: row.reserved }} />
      </p>
      {row.product === 'engine' ? (
        <p>
          <RichMessage
            source={'{slot0} or inspect the reported library item’s validation.'}
            slots={{
              slot0: (
                <Link to="/admin/matches?verification=failed">
                  {t('Inspect match verification')}
                </Link>
              ),
            }}
          />
        </p>
      ) : (
        <>
          <button disabled={busy} onClick={() => void inspect()}>
            {t('Inspect recovery details')}
          </button>
          {details && <RecoveryDetails details={details} />}
          <p>
            {metered
              ? t(
                  'Enter usage verified against provider evidence. Input totals include cache creation/write tokens. Unknown usage must not be entered as zero.',
                )
              : t(
                  'Returning the reservation marks this generation failed. Inspect its details and confirm delivery cannot be recovered first.',
                )}
          </p>
          <fieldset disabled={busy}>
            <label className="field">
              {t('Reason and evidence')}
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
                        input: t('Input tokens (including cached)'),
                        cachedInput: t('Cached input tokens'),
                        cacheWrite: 'Cache creation/write tokens',
                        output: t('Output tokens'),
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
                    {t('Copy recorded usage')}
                  </button>
                )}
                <label>
                  <input
                    type="checkbox"
                    checked={verified}
                    onChange={(e) => setVerified(e.target.checked)}
                  />{' '}
                  {t(
                    'I checked these token counts against provider evidence, including any explicitly entered zeros.',
                  )}
                </label>
              </>
            )}
            <button disabled={!ready} onClick={() => void recover()}>
              {metered ? t('Reconcile verified usage') : t('Return reserved credit')}
            </button>
          </fieldset>
        </>
      )}
      {busy && <p role="status">{t('Working…')}</p>}
      {error && <p role="alert">{displayMessage(error)}</p>}
    </article>
  );
}
export function Operations() {
  useLocale();
  const { values, set } = useAdminFilters();
  const load = useLoad(
    (signal) =>
      request<AdminOperations>('GET', '/api/v1/admin/operations', { signal, query: values }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>{t('Operations')}</h2>
      <label>
        {t('Product')}{' '}
        <select value={values['product'] ?? ''} onChange={(e) => set({ product: e.target.value })}>
          <option value="">{t('All')}</option>
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
      <button onClick={load.reload}>{t('Refresh')}</button>
      <Loaded load={load}>
        {(data) => (
          <>
            <p>
              <RichMessage
                source={'Oldest queued engine job: {slot0}'}
                slots={{
                  slot0:
                    data.queueAgeSeconds === null
                      ? t('none')
                      : t('{value0} seconds', { value0: Math.round(data.queueAgeSeconds) }),
                }}
              />
            </p>
            <h3>{t('Reserved product credits')}</h3>
            {data.reservedCredits.map((r) => (
              <p key={r.product}>
                <RichMessage
                  source={'{slot0}: {slot1}'}
                  slots={{ slot0: productName(r.product), slot1: r.reserved }}
                />
              </p>
            ))}
            <h3>{t('Engine agents')}</h3>
            {data.agents.length === 0 && <p>{t('No engine agents are registered.')}</p>}
            {data.agents.map((a) => (
              <p key={a.id}>
                <RichMessage
                  source={'{slot0} · last seen {slot1}{slot2}'}
                  slots={{
                    slot0: a.id,
                    slot1: dateTime(a.lastSeenAt),
                    slot2: Date.now() - Date.parse(a.lastSeenAt) > 90000 ? t(' · stale') : '',
                  }}
                />
              </p>
            ))}
            <h3>{t('Worker leases')}</h3>
            {data.workers.length === 0 && <p>{t('No worker leases are recorded.')}</p>}
            {data.workers.map((w) => (
              <p key={w.name}>
                <RichMessage
                  source={'{slot0} · {slot1} · last renewed {slot2}{slot3}'}
                  slots={{
                    slot0: w.name,
                    slot1: w.holder,
                    slot2: dateTime(w.renewedAt),
                    slot3: Date.now() - Date.parse(w.renewedAt) > 90000 ? t(' · stale') : '',
                  }}
                />
              </p>
            ))}
            <h3>{t('Requests needing attention')}</h3>
            <p>{t('Uncertain requests and queued/failed engine jobs, oldest first.')}</p>
            {data.items.length === 0 ? (
              <p>{t('No requests need attention.')}</p>
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
