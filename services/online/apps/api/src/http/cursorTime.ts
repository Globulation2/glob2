import { sql, type RawBuilder } from 'kysely';

/** Keep PostgreSQL microseconds; JavaScript Date alone loses pagination ties. */
export function cursorTime(value: Date | string): string {
  const parsed = new Date(value);
  if (!Number.isFinite(parsed.getTime())) throw new Error('Invalid cursor timestamp.');
  const exact =
    typeof value === 'string' && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{1,6}Z$/.test(value)
      ? value
      : parsed.toISOString();
  return exact.replace(
    /\.(\d+)Z$/,
    (_match, fraction: string) => '.' + fraction.padEnd(6, '0') + 'Z',
  );
}

/** Select cursor timestamps as text before the pg driver converts them to Date. */
export function cursorTimeSql(value: RawBuilder<Date>): RawBuilder<string> {
  return sql<string>`to_char(${value} AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"')`;
}
