import type { AdminFinances } from '@glob2/protocol';
import { request } from '../api.ts';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { useAdminFilters } from './Moderation.tsx';
import { downloadCsv } from './Overview.tsx';
import { dateTime } from '../format.ts';
export function Finances() {
  const { values, set } = useAdminFilters({ days: '30', mode: 'live' });
  const load = useLoad(
    (signal) => request<AdminFinances>('GET', '/api/v1/admin/finances', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>Revenue and costs</h2>
      <div className="toolbar">
        <label>
          Range{' '}
          <select value={values['days']} onChange={(e) => set({ days: e.target.value })}>
            {[7, 30, 90].map((d) => (
              <option key={d} value={d}>
                {d} days
              </option>
            ))}
          </select>
        </label>
        <label>
          Payment mode{' '}
          <select value={values['mode']} onChange={(e) => set({ mode: e.target.value })}>
            {['live', 'test', 'unclassified'].map((m) => (
              <option key={m}>{m}</option>
            ))}
          </select>
        </label>
        <button onClick={load.reload}>Refresh</button>
      </div>
      <Loaded load={load}>
        {(data) => {
          const currencies = [...new Set(data.cash.map((c) => c.currency))];
          return (
            <>
              <p>
                Updated {dateTime(data.generatedAt)} · UTC periods · {data.mode} payments
              </p>
              <p>
                Provider estimates include all recorded attempts in this period, regardless of
                payment mode. Payment fees, hosting, exchange rates and profit are excluded.
              </p>
              {data.historicalIncomplete && (
                <p className="notice warn">
                  Historical amounts or payment modes are incomplete. {data.unknownPurchases}{' '}
                  purchases have incomplete historical facts. Refund money is never inferred from
                  reversed credits.
                </p>
              )}
              <h3>Confirmed cash</h3>
              {!data.cash.length && <p>No confirmed cash events in this range and payment mode.</p>}
              {currencies.map((currency) => {
                const total = (kind: string) =>
                  data.cash
                    .filter(
                      (r) => r.currency === currency && r.kind === kind && r.period === 'current',
                    )
                    .reduce((n, r) => n + r.amount, 0);
                return (
                  <p key={currency}>
                    {currency.toUpperCase()} (minor units): collected {total('payment')} · refunded{' '}
                    {total('refund')} · collected less refunds {total('payment') - total('refund')}
                  </p>
                );
              })}
              <div
                tabIndex={0}
                role="region"
                aria-label="Confirmed cash table"
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <caption>
                    Cash amounts in currency minor units; current and previous periods.
                  </caption>
                  <thead>
                    <tr>
                      <th>Product</th>
                      <th>Currency</th>
                      <th>Period</th>
                      <th>Event</th>
                      <th>Amount</th>
                      <th>Count</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.cash.map((r, i) => (
                      <tr key={i}>
                        <th>{r.product}</th>
                        <td>{r.currency}</td>
                        <td>{r.period}</td>
                        <td>{r.kind}</td>
                        <td>{r.amount}</td>
                        <td>{r.events}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              <h3>Product credits</h3>
              <p>
                Each product has its own credit unit. Usage/refund ledger entries are signed;
                reserved balances are current snapshots.
              </p>
              <table>
                <thead>
                  <tr>
                    <th>Product</th>
                    <th>Flow</th>
                    <th>Credits</th>
                  </tr>
                </thead>
                <tbody>
                  {data.credits.map((r, i) => (
                    <tr key={i}>
                      <th>{r.product}</th>
                      <td>{r.kind}</td>
                      <td>{r.amount}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
              <button
                onClick={() =>
                  downloadCsv('glob2-credit-flows.csv', [
                    ['Product', 'Flow', 'Credits'],
                    ...data.credits.map((r) => [r.product, r.kind, r.amount]),
                  ])
                }
              >
                Export credits CSV
              </button>
              <h3>Estimated provider costs</h3>
              <div
                tabIndex={0}
                role="region"
                aria-label="Provider cost table"
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <caption>
                    Estimates use measured usage and configured, versioned monetary rates. Missing
                    usage/pricing remains unavailable.
                  </caption>
                  <thead>
                    <tr>
                      <th>Product / model</th>
                      <th>Period</th>
                      <th>Attempts</th>
                      <th>Measured</th>
                      <th>Priced</th>
                      <th>Unavailable cost</th>
                      <th>Estimate</th>
                      <th>Rate</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.costs.map((r, i) => (
                      <tr key={i}>
                        <th>
                          {r.product} / {r.model}
                        </th>
                        <td>{r.period}</td>
                        <td>{r.attempts}</td>
                        <td>{r.metered}</td>
                        <td>{r.priced}</td>
                        <td>
                          {r.attempts - r.priced} (
                          {r.attempts
                            ? Math.round((100 * (r.attempts - r.priced)) / r.attempts)
                            : 0}
                          %)
                        </td>
                        <td>
                          {r.estimatedMicros === null
                            ? 'Unavailable'
                            : `${(r.estimatedMicros / 1000000).toFixed(6)} ${r.currency}`}
                        </td>
                        <td>{r.rateVersion ?? 'Unavailable'}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              <button
                onClick={() =>
                  downloadCsv('glob2-provider-costs.csv', [
                    [
                      'Product',
                      'Model',
                      'Period',
                      'Currency',
                      'Rate version',
                      'Attempts',
                      'Measured',
                      'Priced',
                      'Estimated micros',
                    ],
                    ...data.costs.map((r) => [
                      r.product,
                      r.model,
                      r.period,
                      r.currency ?? 'Unavailable',
                      r.rateVersion ?? 'Unavailable',
                      r.attempts,
                      r.metered,
                      r.priced,
                      r.estimatedMicros ?? 'Unavailable',
                    ]),
                  ])
                }
              >
                Export provider costs CSV
              </button>
              <button
                onClick={() =>
                  downloadCsv('glob2-finances.csv', [
                    [
                      'Mode',
                      'Product',
                      'Currency',
                      'Period',
                      'Kind',
                      'Amount minor units',
                      'Events',
                    ],
                    ...data.cash.map((r) => [
                      r.mode,
                      r.product,
                      r.currency,
                      r.period,
                      r.kind,
                      r.amount,
                      r.events,
                    ]),
                  ])
                }
              >
                Export cash CSV
              </button>
            </>
          );
        }}
      </Loaded>
    </section>
  );
}
