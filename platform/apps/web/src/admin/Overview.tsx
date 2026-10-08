import type { AdminAnalytics } from '@glob2/protocol';
import { request } from '../api.ts';
import { LineChart } from '../components/LineChart.tsx';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { Link } from '../router.tsx';
import { useAdminFilters } from './Moderation.tsx';
import { dateTime } from '../format.ts';

export function downloadCsv(name: string, rows: readonly (readonly (string | number)[])[]) {
  const csv = rows
    .map((row) =>
      row
        .map(
          (value) =>
            '"' +
            (typeof value === 'string'
              ? value.replace(/^[=+@-]/, "'$&")
              : String(value)
            ).replaceAll('"', '""') +
            '"',
        )
        .join(','),
    )
    .join('\r\n');
  const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' })),
    a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}
function measureName(metric: string, dimension: string) {
  const products: Record<string, string> = {
    maps: 'Map Studio',
    music: 'Music Studio',
    terrain: 'Terrain Studio',
    buildings: 'Building Studio',
    aiStudio: 'AI Studio',
    hive: 'Hive',
    engine: 'Engine jobs',
    ais: 'AIs',
    sets: 'Terrain/resource sets',
    skins: 'Skins',
  };
  if (metric === 'accounts.created')
    return dimension === 'guest' ? 'Guest accounts created' : 'Registered signups';
  if (metric === 'activity')
    return dimension === 'guest' ? 'Guest active account-days' : 'Registered active account-days';
  if (metric.startsWith('matches.')) return 'Matches ' + metric.slice(8);
  if (metric === 'library.published') return (products[dimension] ?? dimension) + ' publications';
  if (metric === 'library.downloads')
    return (products[dimension] ?? dimension) + ' recorded downloads';
  if (metric.startsWith('status.'))
    return (products[metric.slice(7)] ?? metric.slice(7)) + ' · ' + dimension;
  if (metric.startsWith('duration.'))
    return (products[metric.slice(9)] ?? metric.slice(9)) + ' completion ' + dimension;
  return metric + ' ' + dimension;
}
export function Overview() {
  const { values, set } = useAdminFilters({ days: '30' });
  const load = useLoad(
    (signal) =>
      request<AdminAnalytics>('GET', '/api/v1/admin/analytics', {
        query: { days: values['days'] },
        signal,
      }),
    [values['days']],
  );
  return (
    <section>
      <h2>Overview</h2>
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
        <button onClick={load.reload}>Refresh</button>
      </div>
      <Loaded load={load}>
        {(data) => {
          const start = new Date(data.generatedAt);
          start.setUTCDate(start.getUTCDate() - data.days + 1);
          const cutoff = start.toISOString().slice(0, 10);
          const keys = [...new Set(data.metrics.map((m) => m.metric + ' · ' + m.dimension))];
          const current = data.metrics.filter((m) => m.day >= cutoff),
            previous = data.metrics.filter((m) => m.day < cutoff);
          return (
            <>
              <p>
                Updated {dateTime(data.generatedAt)} · UTC days ·{' '}
                {data.collection ? 'Collection enabled' : 'Collection disabled'}
              </p>
              <div
                style={{
                  display: 'grid',
                  gridTemplateColumns: 'repeat(auto-fit,minmax(220px,1fr))',
                  gap: 16,
                }}
              >
                <article className="card">
                  <h3>Players online</h3>
                  <p>
                    {data.live.playersOnline} within {data.live.activeWindowMinutes} minutes
                  </p>
                </article>
                <article className="card">
                  <h3>Matches</h3>
                  <p>
                    {data.live.liveMatches} live · {data.live.matchesToday} in the last 24 hours
                  </p>
                  <Link to="/admin/matches">Inspect matches</Link>
                </article>
                <article className="card">
                  <h3>Active accounts</h3>
                  <p>
                    {data.active.daily} today · {data.active.weekly} in 7 days ·{' '}
                    {data.active.monthly} in 30 days
                  </p>
                  <p>
                    Registered: {data.active.registered.daily} today ·{' '}
                    {data.active.registered.weekly} weekly · {data.active.registered.monthly}{' '}
                    monthly
                  </p>
                  <p>
                    Guests: {data.active.guests.daily} today · {data.active.guests.weekly} weekly ·{' '}
                    {data.active.guests.monthly} monthly
                  </p>
                  <p>Authenticated account activity; anonymous visitors are excluded.</p>
                </article>
                <article className="card">
                  <h3>Human match participants</h3>
                  <p>
                    {data.participants.current} unique accounts · previous period{' '}
                    {data.participants.previous}
                  </p>
                </article>
              </div>
              <p>{data.live.queues?.map((q) => `${q.id}: ${q.searching} searching`).join(' · ')}</p>
              <div className="toolbar">
                <Link to="/admin/reports">Review reports</Link>
                <Link to="/admin/operations">Review operations</Link>
                <Link to="/admin/content">Inspect library content</Link>
                <Link to="/admin/finances">Revenue and costs</Link>
              </div>
              <h3>Needs attention</h3>
              <p>
                {data.attention['reports'] ?? 0} open reports · {data.attention['uncertain'] ?? 0}{' '}
                uncertain requests · {data.attention['failedJobs'] ?? 0} failed engine jobs
              </p>
              <h3>Activity trends</h3>
              <LineChart
                title="Daily account activity"
                series={['guest', 'registered'].map((kind, i) => ({
                  name: kind === 'guest' ? 'Guest accounts' : 'Registered accounts',
                  color: i ? '#41ad83' : '#6d9ee8',
                  points: current
                    .filter((m) => m.metric === 'activity' && m.dimension === kind)
                    .map((m) => ({ x: Date.parse(m.day + 'T00:00:00Z'), y: m.value })),
                }))}
                xFormat={(x) => new Date(x).toISOString().slice(5, 10)}
              />
              <LineChart
                title="Match activity"
                series={['started', 'completed', 'cancelled'].map((kind, i) => ({
                  name: kind,
                  color: ['#41ad83', '#6d9ee8', '#e2a76b'][i] ?? '#41ad83',
                  points: current
                    .filter((m) => m.metric === 'matches.' + kind)
                    .map((m) => ({ x: Date.parse(m.day + 'T00:00:00Z'), y: m.value })),
                }))}
                xFormat={(x) => new Date(x).toISOString().slice(5, 10)}
              />
              <h3>Average completion time</h3>
              {[
                ...new Set(
                  current.filter((m) => m.metric.startsWith('duration.')).map((m) => m.metric),
                ),
              ].map((metric) => {
                const seconds = current
                    .filter((m) => m.metric === metric && m.dimension === 'seconds')
                    .reduce((n, m) => n + m.value, 0),
                  samples = current
                    .filter((m) => m.metric === metric && m.dimension === 'samples')
                    .reduce((n, m) => n + m.value, 0);
                return (
                  <p key={metric}>
                    {measureName(metric, '').replace(' completion ', '')}:{' '}
                    {samples ? Math.round(seconds / samples) + ' seconds' : 'Unavailable'} ·{' '}
                    {samples} completed requests/jobs
                  </p>
                );
              })}
              <button
                onClick={() =>
                  downloadCsv('glob2-activity.csv', [
                    ['UTC day', 'Metric', 'Dimension', 'Value'],
                    ...data.metrics.map((m) => [m.day, m.metric, m.dimension, m.value]),
                  ])
                }
              >
                Export CSV
              </button>
              <div
                tabIndex={0}
                role="region"
                aria-label="Activity comparison table"
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <caption>
                    Current {data.days} days and previous {data.days} days. Activity totals count
                    account-days; the active-account cards count distinct accounts. Studio/job
                    statuses use request creation dates. Publications count versions; recorded
                    downloads use each library’s existing counting rules.
                  </caption>
                  <thead>
                    <tr>
                      <th>Measure</th>
                      <th>Current</th>
                      <th>Previous</th>
                      <th>Change</th>
                    </tr>
                  </thead>
                  <tbody>
                    {keys.map((key) => {
                      const sum = (rows: typeof current) =>
                          rows
                            .filter((m) => m.metric + ' · ' + m.dimension === key)
                            .reduce((n, m) => n + m.value, 0),
                        a = sum(current),
                        b = sum(previous);
                      return (
                        <tr key={key}>
                          <th>
                            {measureName(key.split(' · ')[0] ?? key, key.split(' · ')[1] ?? '')}
                          </th>
                          <td>{a.toLocaleString()}</td>
                          <td>{b.toLocaleString()}</td>
                          <td>{(a - b).toLocaleString()}</td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              </div>
              <details>
                <summary>Daily values</summary>
                <div
                  tabIndex={0}
                  role="region"
                  aria-label="Daily activity table"
                  style={{ overflowX: 'auto' }}
                >
                  <table>
                    <thead>
                      <tr>
                        <th>UTC day</th>
                        <th>Metric</th>
                        <th>Dimension</th>
                        <th>Value</th>
                      </tr>
                    </thead>
                    <tbody>
                      {current.map((m) => (
                        <tr key={m.day + m.metric + m.dimension}>
                          <td>{m.day}</td>
                          <td>{measureName(m.metric, m.dimension)}</td>
                          <td>{m.dimension}</td>
                          <td>{m.value.toLocaleString()}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </details>
              <h3>Top library content by recorded downloads</h3>
              {data.topContent.map((c) => (
                <p key={c.library + c.id}>
                  <Link to={c.href}>{c.name}</Link> · {c.library} · {c.downloads ?? 'Unavailable'}{' '}
                  downloads
                </p>
              ))}
              <details>
                <summary>Historical coverage</summary>
                <p>
                  Missing historical measurements are unavailable, rather than reconstructed from
                  last-seen timestamps. Current periods may be partial. Retained account markers
                  cover 90 days; anonymous daily totals cover 24 months.
                </p>
                {data.coverage.map((c) => (
                  <p key={c.metric}>
                    {c.metric}: since {c.since}
                    {c.historicalIncomplete ? ' · incomplete historical data' : ''}
                  </p>
                ))}
              </details>
            </>
          );
        }}
      </Loaded>
    </section>
  );
}
