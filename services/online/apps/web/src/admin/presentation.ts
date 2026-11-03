import { getLocale } from '../messages.ts';
import { t } from '../messages.ts';
import { statusLabel } from '../messages.ts';
import type { AdminAnalytics } from '@glob2/protocol';

export const PRODUCT_NAMES: Readonly<Record<string, string>> = {
  maps: 'Map Studio',
  music: 'Music Studio',
  terrain: 'Terrain Studio',
  buildings: 'Building Studio',
  aiStudio: 'AI Studio',
  generatorStudio: 'Generator Studio',
  hive: 'Hive',
  engine: 'Engine jobs',
};
export function productName(product: string) {
  return Object.hasOwn(PRODUCT_NAMES, product) ? t(PRODUCT_NAMES[product]) : product;
}
export const PAYMENT_MODE_NAMES: Readonly<Record<string, string>> = {
  live: 'Live payments',
  test: 'Test payments',
  unclassified: 'Unclassified payments',
};
export const REPORTING_PERIOD_NAMES: Readonly<Record<string, string>> = {
  current: 'Current period',
  previous: 'Previous period',
};
export const FINANCE_KIND_NAMES: Readonly<Record<string, string>> = {
  payment: 'Payment',
  purchase: 'Purchase',
  usage: 'Usage',
  refund: 'Refund',
  reserved: 'Reserved',
  adjustment: 'Account adjustment',
  grant: 'Credit grant',
  returned: 'Returned credits',
  dispute: 'Dispute',
  dispute_closed: 'Dispute closed',
};
export const MATCH_METRIC_NAMES: Readonly<Record<string, string>> = {
  'matches.started': 'Matches started',
  'matches.completed': 'Matches completed',
  'matches.cancelled': 'Matches cancelled',
};
export const COMPLETION_METRIC_NAMES: Readonly<Record<string, string>> = {
  seconds: '{value0} completion time (seconds)',
  samples: '{value0} completion samples',
};
export function paymentModeName(mode: string) {
  return Object.hasOwn(PAYMENT_MODE_NAMES, mode) ? t(PAYMENT_MODE_NAMES[mode]) : mode;
}
export function reportingPeriodName(period: string) {
  return Object.hasOwn(REPORTING_PERIOD_NAMES, period) ? t(REPORTING_PERIOD_NAMES[period]) : period;
}
export function financeKindName(kind: string) {
  return Object.hasOwn(FINANCE_KIND_NAMES, kind) ? t(FINANCE_KIND_NAMES[kind]) : kind;
}
export function libraryName(library: string) {
  return (
    (
      {
        maps: t('Maps'),
        ais: t('AI'),
        generators: t('Generators'),
        buildings: t('Buildings'),
        sets: t('Terrain & resources'),
        skins: t('Skins'),
        music: t('Music'),
      } as Record<string, string>
    )[library] ?? library
  );
}
export function measureName(metric: string, dimension: string) {
  if (metric === 'accounts.created')
    return dimension === 'guest' ? t('Guest accounts created') : t('Registered signups');
  if (metric === 'activity')
    return dimension === 'guest'
      ? t('Guest active account-days')
      : t('Registered active account-days');
  if (Object.hasOwn(MATCH_METRIC_NAMES, metric)) return t(MATCH_METRIC_NAMES[metric]);
  if (metric === 'library.published')
    return t('{value0} publications', { value0: libraryName(dimension) });
  if (metric === 'library.downloads')
    return t('{value0} recorded downloads', { value0: libraryName(dimension) });
  if (metric.startsWith('status.'))
    return productName(metric.slice(7)) + ' · ' + statusLabel(dimension);
  if (metric.startsWith('duration.') && Object.hasOwn(COMPLETION_METRIC_NAMES, dimension))
    return t(COMPLETION_METRIC_NAMES[dimension], { value0: productName(metric.slice(9)) });
  return metric + ' ' + dimension;
}
/** Preserve exact units in exports, and neutralize spreadsheet formulas in string cells. */
export function csvText(rows: readonly (readonly (string | number)[])[]) {
  return rows
    .map((row) =>
      row
        .map(
          (value) =>
            '"' +
            (typeof value === 'string'
              ? value.replace(/^(\s*)([=+@-])/, "'$1$2")
              : String(value)
            ).replaceAll('"', '""') +
            '"',
        )
        .join(','),
    )
    .join('\r\n');
}
export function downloadCsv(name: string, rows: readonly (readonly (string | number)[])[]) {
  const url = URL.createObjectURL(new Blob([csvText(rows)], { type: 'text/csv;charset=utf-8' }));
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  anchor.click();
  URL.revokeObjectURL(url);
}
// Stripe charge/refund units differ from ISO currency decimals (notably MGA,
// ISK and UGX). Payout-only HUF/TWD rules do not apply to this cash journal.
// https://docs.stripe.com/currencies#zero-decimal and #special-cases
const ZERO_DECIMAL_CHARGES = new Set([
  'bif',
  'clp',
  'djf',
  'gnf',
  'jpy',
  'kmf',
  'krw',
  'mga',
  'pyg',
  'rwf',
  'vnd',
  'vuv',
  'xaf',
  'xof',
  'xpf',
]);
export function formatCash(amount: number, currency: string) {
  const digits = ZERO_DECIMAL_CHARGES.has(currency.toLowerCase()) ? 0 : 2;
  return new Intl.NumberFormat(getLocale(), {
    style: 'currency',
    currency: currency.toUpperCase(),
    currencyDisplay: 'code',
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  }).format(amount / 10 ** digits);
}

export function reportingPeriods(generatedAt: string, days: number) {
  const day = new Date(generatedAt.slice(0, 10) + 'T00:00:00Z');
  const at = (offset: number) =>
    new Date(day.getTime() - offset * 86400000).toISOString().slice(0, 10);
  return {
    today: at(0),
    currentStart: at(days - 1),
    previousStart: at(2 * days - 1),
    previousEnd: at(days),
  };
}

/** Roll up successful completion observations as a weighted mean, not a mean of daily means. */
export function completionStats(
  metrics: readonly AdminAnalytics['metrics'][number][],
  metric: string,
) {
  let seconds = 0,
    samples = 0;
  for (const row of metrics) {
    if (row.metric !== metric) continue;
    if (row.dimension === 'seconds') seconds += row.value;
    if (row.dimension === 'samples') samples += row.value;
  }
  return { mean: samples > 0 ? seconds / samples : null, samples };
}
