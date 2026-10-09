import { statusLabel } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import type { AdminFinances } from '@glob2/protocol';
import { request } from '../api.ts';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { useAdminFilters } from './filters.tsx';
import { downloadCsv, formatCash, productName, reportingPeriods } from './presentation.ts';
import { dateTime } from '../format.ts';
export function Finances() {
  useLocale();
  const { values, set } = useAdminFilters({ days: '30', mode: 'live' });
  const load = useLoad(
    (signal) => request<AdminFinances>('GET', '/api/v1/admin/finances', { query: values, signal }),
    [JSON.stringify(values)],
  );
  return (
    <section>
      <h2>{t('Revenue and costs')}</h2>
      <div className="toolbar">
        <label>
          {t('Range')}{' '}
          <select value={values['days']} onChange={(e) => set({ days: e.target.value })}>
            {[7, 30, 90].map((d) => (
              <option key={d} value={d}>
                <RichMessage
                  source={'{slot0} days'}
                  slots={{ slot0: d }}
                  singular={'{slot0} day'}
                  count={Number(d)}
                />
              </option>
            ))}
          </select>
        </label>
        <label>
          {t('Payment mode')}{' '}
          <select value={values['mode']} onChange={(e) => set({ mode: e.target.value })}>
            {['live', 'test', 'unclassified'].map((m) => (
              <option key={m}>{m}</option>
            ))}
          </select>
        </label>
        <button onClick={load.reload}>{t('Refresh')}</button>
      </div>
      <Loaded load={load}>
        {(data) => {
          const periods = reportingPeriods(data.generatedAt, data.days);
          const currencies = [...new Set(data.cash.map((c) => c.currency))];
          return (
            <>
              <p>
                <RichMessage
                  source={'Updated {slot0} · UTC periods · {slot1} payments'}
                  slots={{ slot0: dateTime(data.generatedAt), slot1: data.mode }}
                />
              </p>
              <p className="notice">
                <RichMessage
                  source={
                    'Current: {slot0} through {slot1} (UTC; today is partial). Previous: {slot2} through {slot3}. Historical coverage may differ between periods.'
                  }
                  slots={{
                    slot0: periods.currentStart,
                    slot1: periods.today,
                    slot2: periods.previousStart,
                    slot3: periods.previousEnd,
                  }}
                />
              </p>
              <p>
                {t(
                  'Credit flows and provider estimates include all recorded activity in this period, regardless of payment mode. Payment fees, hosting, exchange rates and profit are excluded.',
                )}
              </p>
              {data.historicalIncomplete && (
                <p className="notice warn">
                  <RichMessage
                    source={
                      'Historical amounts or payment modes are incomplete. {slot0} purchases have incomplete historical facts. Refund money is never inferred from reversed credits.'
                    }
                    slots={{ slot0: data.unknownPurchases }}
                  />
                </p>
              )}
              <h3>{t('Confirmed cash')}</h3>
              {!data.cash.length && (
                <p>{t('No confirmed cash events in this range and payment mode.')}</p>
              )}
              {currencies.map((currency) => {
                const total = (kind: string) =>
                  data.cash
                    .filter(
                      (r) => r.currency === currency && r.kind === kind && r.period === 'current',
                    )
                    .reduce((n, r) => n + r.amount, 0);
                return (
                  <p key={currency}>
                    <RichMessage
                      source={
                        '{slot0}: collected {slot1} · refunded {slot2} · collected less refunds {slot3}'
                      }
                      slots={{
                        slot0: currency.toUpperCase(),
                        slot1: formatCash(total(t('payment')), currency),
                        slot2: formatCash(total(t('refund')), currency),
                        slot3: formatCash(total(t('payment')) - total(t('refund')), currency),
                      }}
                    />
                  </p>
                );
              })}
              <div
                tabIndex={0}
                role="region"
                aria-label={t('Confirmed cash table')}
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <caption>
                    {t(
                      'Confirmed cash uses Stripe charge/refund currency units; current and previous UTC periods. CSV exports preserve exact integer minor units.',
                    )}
                  </caption>
                  <thead>
                    <tr>
                      <th>{t('Product')}</th>
                      <th>{t('Currency')}</th>
                      <th>{t('Period')}</th>
                      <th>{t('Event')}</th>
                      <th className="numeric">{t('Cash amount')}</th>
                      <th className="numeric">{t('Count')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.cash.map((r, i) => (
                      <tr key={i}>
                        <th>{productName(r.product)}</th>
                        <td>{r.currency.toUpperCase()}</td>
                        <td>{r.period}</td>
                        <td>{statusLabel(r.kind)}</td>
                        <td
                          className="numeric"
                          title={t('{value0} Stripe minor units', { value0: r.amount })}
                        >
                          {formatCash(r.amount, r.currency)}
                        </td>
                        <td className="numeric">{r.events}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
              <h3>{t('Product credits')}</h3>
              <p>
                {t(
                  'Each product has its own credit unit. Flows cover the current selected range across all payment modes. Usage/refund ledger entries are signed; reserved balances are current snapshots.',
                )}
              </p>
              <div
                tabIndex={0}
                role="region"
                aria-label={t('Product credit flow table')}
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <thead>
                    <tr>
                      <th>{t('Product')}</th>
                      <th>{t('Flow')}</th>
                      <th className="numeric">{t('Credits')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.credits.map((r, i) => (
                      <tr key={i}>
                        <th>{productName(r.product)}</th>
                        <td>{statusLabel(r.kind)}</td>
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
                {t('Export credits CSV')}
              </button>
              <h3>{t('Estimated provider costs')}</h3>
              <div
                tabIndex={0}
                role="region"
                aria-label={t('Provider cost table')}
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <caption>
                    {t(
                      'Estimates use measured usage and configured, versioned monetary rates. Missing usage/pricing remains unavailable. An estimate covers priced attempts only; it is a partial subtotal whenever any attempt has unavailable cost.',
                    )}
                  </caption>
                  <thead>
                    <tr>
                      <th>{t('Product / model')}</th>
                      <th>{t('Period')}</th>
                      <th className="numeric">{t('Attempts')}</th>
                      <th className="numeric">{t('Usage measured')}</th>
                      <th className="numeric">{t('Priced')}</th>
                      <th className="numeric">{t('Unavailable cost')}</th>
                      <th className="numeric">{t('Known-cost subtotal')}</th>
                      <th>{t('Rate')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.costs.map((r, i) => (
                      <tr key={i}>
                        <th>
                          <RichMessage
                            source={'{slot0} / {slot1}'}
                            slots={{ slot0: productName(r.product), slot1: r.model }}
                          />
                        </th>
                        <td>{r.period}</td>
                        <td className="numeric">{r.attempts}</td>
                        <td className="numeric">{r.metered}</td>
                        <td className="numeric">{r.priced}</td>
                        <td className="numeric">
                          <RichMessage
                            source={'{slot0} ({slot1}%)'}
                            slots={{
                              slot0: r.attempts - r.priced,
                              slot1: r.attempts
                                ? Math.round((100 * (r.attempts - r.priced)) / r.attempts)
                                : 0,
                            }}
                          />
                        </td>
                        <td className="numeric">
                          {r.estimatedMicros === null
                            ? t('Unavailable')
                            : `${(r.estimatedMicros / 1000000).toFixed(6)} ${r.currency}`}
                        </td>
                        <td>{r.rateVersion ?? t('Unavailable')}</td>
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
                {t('Export provider costs CSV')}
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
                {t('Export cash CSV')}
              </button>
            </>
          );
        }}
      </Loaded>
    </section>
  );
}
