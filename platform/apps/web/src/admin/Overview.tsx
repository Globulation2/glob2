import { getLocale } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import type { AdminAnalytics } from '@glob2/protocol';
import { request } from '../api.ts';
import { LineChart } from '../components/LineChart.tsx';
import { Loaded } from '../components/common.tsx';
import { useLoad } from '../state.tsx';
import { Link } from '../router.tsx';
import { useAdminFilters } from './filters.tsx';
import { dateTime } from '../format.ts';
import { downloadCsv, measureName, productName, completionStats } from './presentation.ts';

export function Overview() {
  useLocale();
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
      <h2>{t('Overview')}</h2>
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
        <button onClick={load.reload}>{t('Refresh')}</button>
      </div>
      <Loaded load={load}>
        {(data) => {
          const start = new Date(data.generatedAt);
          start.setUTCDate(start.getUTCDate() - data.days + 1);
          const cutoff = start.toISOString().slice(0, 10);
          const today = data.generatedAt.slice(0, 10);
          const xDomain = [
            Date.parse(cutoff + 'T00:00:00Z'),
            Date.parse(today + 'T00:00:00Z'),
          ] as const;
          const keys = [
            ...new Set(
              data.metrics
                .filter((m) => !m.metric.startsWith('duration.'))
                .map((m) => m.metric + ' · ' + m.dimension),
            ),
          ];
          const current = data.metrics.filter((m) => m.day >= cutoff),
            previous = data.metrics.filter((m) => m.day < cutoff);
          return (
            <>
              <p>
                <RichMessage
                  source={'Updated {slot0} · UTC days · {slot1}'}
                  slots={{
                    slot0: dateTime(data.generatedAt),
                    slot1: data.collection ? t('Collection enabled') : t('Collection disabled'),
                  }}
                />
              </p>
              <p className="notice">
                <RichMessage
                  source={
                    'Reporting range: {slot0} through {slot1} (UTC). Today is still in progress; the previous {slot2} days are complete calendar days. History may be incomplete; comparisons use recorded observations only.'
                  }
                  slots={{ slot0: cutoff, slot1: today, slot2: data.days }}
                  singular={
                    'Reporting range: {slot0} through {slot1} (UTC). Today is still in progress; the previous {slot2} day are complete calendar days. History may be incomplete; comparisons use recorded observations only.'
                  }
                  count={Number(data.days)}
                />
              </p>
              <div
                style={{
                  display: 'grid',
                  gridTemplateColumns: 'repeat(auto-fit,minmax(220px,1fr))',
                  gap: 16,
                }}
              >
                <article className="card">
                  <h3>{t('Players online')}</h3>
                  <p>
                    <RichMessage
                      source={'{slot0} within {slot1} minutes'}
                      slots={{
                        slot0: data.live.playersOnline,
                        slot1: data.live.activeWindowMinutes,
                      }}
                    />
                  </p>
                </article>
                <article className="card">
                  <h3>{t('Matches')}</h3>
                  <p>
                    <RichMessage
                      source={'{slot0} live · {slot1} in the last 24 hours'}
                      slots={{ slot0: data.live.liveMatches, slot1: data.live.matchesToday }}
                    />
                  </p>
                  <Link to="/admin/matches">{t('Inspect matches')}</Link>
                </article>
                <article className="card">
                  <h3>{t('Active accounts')}</h3>
                  <p>
                    <RichMessage
                      source={'{slot0} today · {slot1} in 7 days · {slot2} in 30 days'}
                      slots={{
                        slot0: data.active.daily,
                        slot1: data.active.weekly,
                        slot2: data.active.monthly,
                      }}
                    />
                  </p>
                  <p>
                    <RichMessage
                      source={'Registered: {slot0} today · {slot1} weekly · {slot2} monthly'}
                      slots={{
                        slot0: data.active.registered.daily,
                        slot1: data.active.registered.weekly,
                        slot2: data.active.registered.monthly,
                      }}
                    />
                  </p>
                  <p>
                    <RichMessage
                      source={'Guests: {slot0} today · {slot1} weekly · {slot2} monthly'}
                      slots={{
                        slot0: data.active.guests.daily,
                        slot1: data.active.guests.weekly,
                        slot2: data.active.guests.monthly,
                      }}
                    />
                  </p>
                  <p>{t('Authenticated account activity; anonymous visitors are excluded.')}</p>
                </article>
                <article className="card">
                  <h3>{t('Human match participants')}</h3>
                  <p>
                    <RichMessage
                      source={'{slot0} unique accounts · previous period {slot1}'}
                      slots={{
                        slot0: data.participants.current,
                        slot1: data.participants.previous,
                      }}
                    />
                  </p>
                </article>
              </div>
              <p>
                {data.live.queues
                  ?.map((q) =>
                    t('{value0}: {count} searching', { value0: q.id, count: q.searching }),
                  )
                  .join(' · ')}
              </p>
              <div className="toolbar">
                <Link to="/admin/reports">{t('Review reports')}</Link>
                <Link to="/admin/operations">{t('Review operations')}</Link>
                <Link to="/admin/content">{t('Inspect library content')}</Link>
                <Link to="/admin/finances">{t('Revenue and costs')}</Link>
              </div>
              <h3>{t('Needs attention')}</h3>
              <p>
                <RichMessage
                  source={
                    '{slot0} open reports · {slot1} uncertain requests · {slot2} failed engine jobs'
                  }
                  slots={{
                    slot0: data.attention['reports'] ?? 0,
                    slot1: data.attention['uncertain'] ?? 0,
                    slot2: data.attention['failedJobs'] ?? 0,
                  }}
                />
              </p>
              <h3>{t('Activity trends')}</h3>
              <p>
                {t(
                  'Each point is a recorded UTC day. Gaps have no recorded measurement and are not interpolated or treated as zero. Hover or touch a point for its value; the daily table below provides the same values.',
                )}
              </p>
              <LineChart
                title={t('Daily active accounts')}
                xDomain={xDomain}
                maxGap={86400000}
                dots
                xLabel="UTC day"
                yLabel="Accounts per day"
                series={['guest', 'registered'].map((kind, i) => ({
                  name: kind === 'guest' ? 'Guest accounts' : 'Registered accounts',
                  color: i ? 'var(--success)' : 'var(--gold-ink)',
                  dashed: kind === 'guest',
                  points: current
                    .filter((m) => m.metric === 'activity' && m.dimension === kind)
                    .map((m) => ({ x: Date.parse(m.day + 'T00:00:00Z'), y: m.value })),
                }))}
                xFormat={(x) => new Date(x).toISOString().slice(5, 10)}
              />
              <LineChart
                title={t('Match activity')}
                xDomain={xDomain}
                maxGap={86400000}
                dots
                xLabel="UTC day"
                yLabel="Matches per day"
                series={['started', 'completed', 'cancelled'].map((kind, i) => ({
                  name: kind[0]?.toUpperCase() + kind.slice(1),
                  dashed: kind === 'cancelled',
                  color: ['var(--success)', 'var(--ink)', 'var(--warn)'][i] ?? 'var(--ink)',
                  points: current
                    .filter((m) => m.metric === 'matches.' + kind)
                    .map((m) => ({ x: Date.parse(m.day + 'T00:00:00Z'), y: m.value })),
                }))}
                xFormat={(x) => new Date(x).toISOString().slice(5, 10)}
              />
              <h3>{t('Average successful completion time')}</h3>
              <p>
                {t(
                  'Measured from creation to completion, grouped by request creation day. Only successful requests/jobs with a known completion time contribute; late completions update their original period.',
                )}
              </p>
              <div
                tabIndex={0}
                role="region"
                aria-label={t('Completion time comparison table')}
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <caption>
                    {t(
                      'Mean seconds per successful delivery, with the number of measured completions.',
                    )}
                  </caption>
                  <thead>
                    <tr>
                      <th>{t('Product')}</th>
                      <th className="numeric">{t('Current mean')}</th>
                      <th className="numeric">{t('Current samples')}</th>
                      <th className="numeric">{t('Previous mean')}</th>
                      <th className="numeric">{t('Previous samples')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    {[
                      ...new Set(
                        data.metrics
                          .filter((m) => m.metric.startsWith('duration.'))
                          .map((m) => m.metric),
                      ),
                    ].map((metric) => {
                      const a = completionStats(current, metric),
                        b = completionStats(previous, metric);
                      const meanText = (mean: number | null) =>
                        mean === null
                          ? t('Unavailable')
                          : mean.toLocaleString(getLocale(), { maximumFractionDigits: 1 }) + ' s';
                      return (
                        <tr key={metric}>
                          <th>{productName(metric.slice(9))}</th>
                          <td className="numeric">{meanText(a.mean)}</td>
                          <td className="numeric">{a.samples}</td>
                          <td className="numeric">{meanText(b.mean)}</td>
                          <td className="numeric">{b.samples}</td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              </div>
              <button
                onClick={() =>
                  downloadCsv('glob2-activity.csv', [
                    ['UTC day', 'Metric', 'Dimension', 'Value'],
                    ...data.metrics.map((m) => [m.day, m.metric, m.dimension, m.value]),
                  ])
                }
              >
                {t('Export activity CSV')}
              </button>
              <div
                tabIndex={0}
                role="region"
                aria-label={t('Activity comparison table')}
                style={{ overflowX: 'auto' }}
              >
                <table>
                  <caption>
                    <RichMessage
                      source={
                        'Current {slot0} days and previous {slot1} days. Activity totals count account-days; the active-account cards count distinct accounts. Studio/job statuses use request creation dates. Publications count versions; recorded downloads use each library’s existing counting rules.'
                      }
                      slots={{ slot0: data.days, slot1: data.days }}
                      singular={
                        'Current {slot0} day and previous {slot1} days. Activity totals count account-days; the active-account cards count distinct accounts. Studio/job statuses use request creation dates. Publications count versions; recorded downloads use each library’s existing counting rules.'
                      }
                      count={Number(data.days)}
                    />
                  </caption>
                  <thead>
                    <tr>
                      <th>{t('Measure')}</th>
                      <th className="numeric">{t('Current recorded')}</th>
                      <th className="numeric">{t('Previous recorded')}</th>
                      <th className="numeric">{t('Change')}</th>
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
                          <td className="numeric">{a.toLocaleString(getLocale())}</td>
                          <td className="numeric">{b.toLocaleString(getLocale())}</td>
                          <td className="numeric">{(a - b).toLocaleString(getLocale())}</td>
                        </tr>
                      );
                    })}
                  </tbody>
                </table>
              </div>
              <details>
                <summary>{t('Daily values')}</summary>
                <div
                  tabIndex={0}
                  role="region"
                  aria-label={t('Daily activity table')}
                  style={{ overflowX: 'auto' }}
                >
                  <table>
                    <thead>
                      <tr>
                        <th>{t('UTC day')}</th>
                        <th>{t('Metric')}</th>
                        <th>{t('Dimension')}</th>
                        <th className="numeric">{t('Value')}</th>
                      </tr>
                    </thead>
                    <tbody>
                      {current.map((m) => (
                        <tr key={m.day + m.metric + m.dimension}>
                          <td>{m.day}</td>
                          <td>{measureName(m.metric, m.dimension)}</td>
                          <td>{m.dimension}</td>
                          <td className="numeric">{m.value.toLocaleString(getLocale())}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </details>
              <h3>{t('Top library content by lifetime recorded downloads')}</h3>
              <p>
                {t(
                  'All-time library totals; this ranking does not change with the selected reporting range. Counts follow each library’s rules and do not represent unique people.',
                )}
              </p>
              {data.topContent.map((c) => (
                <p key={c.library + c.id}>
                  <RichMessage
                    source={'{slot0} · {slot1} · {slot2} downloads'}
                    slots={{
                      slot0: <Link to={c.href}>{c.name}</Link>,
                      slot1: c.library,
                      slot2: c.downloads ?? t('Unavailable'),
                    }}
                  />
                </p>
              ))}
              <details>
                <summary>{t('Historical coverage')}</summary>
                <p>
                  {t(
                    'Missing historical measurements are unavailable, rather than reconstructed from last-seen timestamps. Current periods may be partial. Retained account markers cover 90 days; anonymous daily totals cover 24 months.',
                  )}
                </p>
                {data.coverage.map((c) => (
                  <p key={c.metric}>
                    <RichMessage
                      source={'{slot0}: since {slot1}{slot2}'}
                      slots={{
                        slot0: c.metric,
                        slot1: c.since,
                        slot2: c.historicalIncomplete ? t(' · incomplete historical data') : '',
                      }}
                    />
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
