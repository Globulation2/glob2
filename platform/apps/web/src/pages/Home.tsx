import { simVersionKey, type QueueInfo } from '@glob2/protocol';
import { api } from '../api.ts';
import { Loaded, MatchListView, PlayerLink } from '../components/common.tsx';
import { rating } from '../format.ts';
import { Link } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';

/** Where the downloadable game lives; a build-time setting (VITE_DOWNLOAD_URL). */
export const DOWNLOAD_URL: string =
  (import.meta.env['VITE_DOWNLOAD_URL'] as string | undefined) ?? 'https://globulation2.org/';

function LadderTeaser({ queue }: { queue: QueueInfo }) {
  const load = useLoad((signal) => api.leaderboard(queue.id, { limit: 5 }, signal), [queue.id]);
  return (
    <div className="card" data-testid="ladder-teaser">
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
        <h3 className="grow">{queue.name}</h3>
        <Link to={`/leaderboard/${queue.id}`} className="caption">
          Full leaderboard
        </Link>
      </div>
      <Loaded load={load}>
        {(page) =>
          page.entries.length === 0 ? (
            <p className="muted">No rated players yet.</p>
          ) : (
            <ol style={{ margin: 0, paddingLeft: 22 }}>
              {page.entries.map((e) => (
                <li key={e.rank}>
                  <span style={{ display: 'flex', gap: 8 }}>
                    <span className="grow ell">
                      {e.entity.kind === 'account' ? (
                        <PlayerLink account={e.entity.account} />
                      ) : null}
                    </span>
                    <span className="num">{rating(e.rating)}</span>
                  </span>
                </li>
              ))}
            </ol>
          )
        }
      </Loaded>
    </div>
  );
}

export function Home() {
  const { instance, instanceError } = useSession();
  const recent = useLoad((signal) => api.matches({ limit: 6 }, signal), []);
  const rated = instance?.queues.filter((q) => q.rated) ?? [];
  return (
    <>
      <div className="page-head">
        <div className="grow">
          <h1>{instance?.name ?? 'Globulation 2'}</h1>
          <div className="caption">
            Online play for Globulation 2: rooms, quick match, rankings and shared maps.
          </div>
        </div>
      </div>
      {instanceError && (
        <div className="notice error" role="alert">
          The platform is unavailable: {instanceError.message}
        </div>
      )}
      <div className="grid2">
        <div className="card">
          <h3>Play now</h3>
          <p className="muted" style={{ marginTop: 0 }}>
            Play straight in your browser, or install the game for desktop and phone. Invite links
            open whichever you have.
          </p>
          <div className="toolbar">
            <a className="btn primary" href="/play/">
              Play in browser
            </a>
            <a className="btn" href={DOWNLOAD_URL} rel="noopener">
              Download the game
            </a>
          </div>
          {instance && (
            <p className="caption" style={{ margin: 0 }}>
              {instance.guestsAllowed
                ? 'Guests may play rooms and casual games; sign in to be ranked.'
                : 'Sign in to play on this instance.'}
            </p>
          )}
        </div>
        <div className="card">
          <h3>This instance</h3>
          {instance ? (
            <>
              <p className="caption" style={{ margin: '0 0 6px' }}>
                {instance.origin}
              </p>
              <div className="caption">Game versions served</div>
              {instance.supportedSimVersions.length === 0 ? (
                <p className="muted" style={{ margin: 0 }}>
                  No game servers are online right now.
                </p>
              ) : (
                <ul style={{ margin: '2px 0 0', paddingLeft: 18 }}>
                  {instance.supportedSimVersions.map((version) => (
                    <li key={simVersionKey(version)}>
                      Format {version.versionMinor}, network protocol {version.netProtocol}
                    </li>
                  ))}
                </ul>
              )}
            </>
          ) : (
            <p className="muted">…</p>
          )}
        </div>
      </div>
      {rated.length > 0 && (
        <>
          <h2>Leaderboards</h2>
          <div className="grid2">
            {rated.map((queue) => (
              <LadderTeaser key={queue.id} queue={queue} />
            ))}
          </div>
        </>
      )}
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
        <h2 className="grow">Recent matches</h2>
        <Link to="/matches" className="caption">
          All matches
        </Link>
      </div>
      <Loaded load={recent}>{(page) => <MatchListView matches={page.items} />}</Loaded>
    </>
  );
}
