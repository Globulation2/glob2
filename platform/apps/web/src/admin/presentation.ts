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
  return PRODUCT_NAMES[product] ?? product;
}
export function libraryName(library: string) {
  return (
    (
      {
        maps: 'Maps',
        ais: 'AIs',
        generators: 'Generators',
        buildings: 'Buildings',
        sets: 'Terrain/resource sets',
        skins: 'Skins',
        music: 'Music',
      } as Record<string, string>
    )[library] ?? library
  );
}
export function measureName(metric: string, dimension: string) {
  if (metric === 'accounts.created')
    return dimension === 'guest' ? 'Guest accounts created' : 'Registered signups';
  if (metric === 'activity')
    return dimension === 'guest' ? 'Guest active account-days' : 'Registered active account-days';
  if (metric.startsWith('matches.')) return 'Matches ' + metric.slice(8);
  if (metric === 'library.published') return libraryName(dimension) + ' publications';
  if (metric === 'library.downloads') return libraryName(dimension) + ' recorded downloads';
  if (metric.startsWith('status.')) return productName(metric.slice(7)) + ' · ' + dimension;
  if (metric.startsWith('duration.'))
    return productName(metric.slice(9)) + ' completion ' + dimension;
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
  return new Intl.NumberFormat(undefined, {
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
