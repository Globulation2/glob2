import type { AdminFinances } from '@glob2/protocol';
import { request } from '../api.ts';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { useAdminFilters } from './filters.tsx';
import { downloadCsv, formatCash, productName, reportingPeriods } from './presentation.ts';
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
          const periods = reportingPeriods(data.generatedAt, data.days);
          const currencies = [...new Set(data.cash.map((c) => c.currency))];
          return (
            <>
              <p>
                Updated {dateTime(data.generatedAt)} · UTC periods · {data.mode} payments
              </p>
              <p className="notice">
                Current: {periods.currentStart} through {periods.today} (UTC; today is partial).
                Previous: {periods.previousStart} through {periods.previousEnd}. Historical coverage
                may differ between periods.
              </p>
              <p>
                Credit flows and provider estimates include all recorded activity in this period,
                regardless of payment mode. Payment fees, hosting, exchange rates and profit are
                excluded.
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
                    {currency.toUpperCase()}: collected {formatCash(total('payment'), currency)} ·
                    refunded {formatCash(total('refund'), currency)} · collected less refunds{' '}
                    {formatCash(total('payment') - total('refund'), currency)}
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
                    Confirmed cash uses Stripe charge/refund currency units; current and previous
                    UTC periods. CSV exports preserve exact integer minor units.
                  </caption>
                  <thead>
                    <tr>
                      <th>Product</th>
                      <th>Currency</th>
                      <th>Period</th>
                      <th>Event</th>
                      <th className="numeric">Cash amount</th>
                      <th className="numeric">Count</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.cash.map((r, i) => (
                      <tr key={i}>
                        <th>{productName(r.product)}</th>
                        <td>{r.currency.toUpperCase()}</td>
                        <td>{r.period}</td>
                        <td>{r.kind}</td>
                        <td className="numeric" title={`${r.amount} Stripe minor units`}>
                          {formatCash(r.amount, r.currency)}
                        </td>
                        <td className="numeric">{r.events}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              <h3>Product credits</h3>
              <p>
                Each product has its own credit unit. Flows cover the current selected range across
                all payment modes. Usage/refund ledger entries are signed; reserved balances are
                current snapshots.
              </p>
              <div
                tabIndex={0}
                role="region"
                aria-label="Product credit flow table"
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <thead>
                    <tr>
                      <th>Product</th>
                      <th>Flow</th>
                      <th className="numeric">Credits</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.credits.map((r, i) => (
                      <tr key={i}>
                        <th>{productName(r.product)}</th>
                        <td>{r.kind}</td>
                        <td className="numeric">{r.amount}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
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
                    usage/pricing remains unavailable. An estimate covers priced attempts only; it
                    is a partial subtotal whenever any attempt has unavailable cost.
                  </caption>
                  <thead>
                    <tr>
                      <th>Product / model</th>
                      <th>Period</th>
                      <th className="numeric">Attempts</th>
                      <th className="numeric">Usage measured</th>
                      <th className="numeric">Priced</th>
                      <th className="numeric">Unavailable cost</th>
                      <th className="numeric">Known-cost subtotal</th>
                      <th>Rate</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.costs.map((r, i) => (
                      <tr key={i}>
                        <th>
                          {productName(r.product)} / {r.model}
                        </th>
                        <td>{r.period}</td>
                        <td className="numeric">{r.attempts}</td>
                        <td className="numeric">{r.metered}</td>
                        <td className="numeric">{r.priced}</td>
                        <td className="numeric">
                          {r.attempts - r.priced} (
                          {r.attempts
                            ? Math.round((100 * (r.attempts - r.priced)) / r.attempts)
                            : 0}
                          %)
                        </td>
                        <td className="numeric">
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
