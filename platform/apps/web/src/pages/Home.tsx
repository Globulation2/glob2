// The home page: the living colony with the ways in (browser, download,
// invite code), live numbers, how the game plays, the leaderboards, maps
// players like, and recent matches.
import { useState, type FormEvent } from 'react';
import type { InstanceStats, MapInfo, QueueInfo } from '@glob2/protocol';
import { api } from '../api.ts';
import { GameArt, Wordmark } from '../art.tsx';
import { ColonyHero } from '../components/Colony.tsx';
import { Avatar, Loaded, MapImage, MatchListView, PlayerLink } from '../components/common.tsx';
import { aiName, rating } from '../format.ts';
import { Link } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';

/**
 * The instance's public website when it is hosted apart from this app (e.g.
 * glob2online.com beside app.glob2online.com); a build-time setting
 * (VITE_WEBSITE_URL). Unset: no website links.
 */
export const WEBSITE_URL: string | undefined =
  ((import.meta.env['VITE_WEBSITE_URL'] as string | undefined) || '').replace(/\/+$/, '') ||
  undefined;

/** A page of the public website, e.g. websitePage('/news/'). */
export function websitePage(path: string): string | undefined {
  return WEBSITE_URL ? `${WEBSITE_URL}${path}` : undefined;
}

/**
 * Where the downloadable game lives: VITE_DOWNLOAD_URL, else the website's
 * downloads page, else the project's release page.
 */
export const DOWNLOAD_URL: string =
  (import.meta.env['VITE_DOWNLOAD_URL'] as string | undefined) ||
  websitePage('/downloads/') ||
  'https://github.com/Globulation2/glob2/releases';

const CODE = /^[A-Za-z0-9]{6,16}$/;

/** An invite code from what was typed or pasted: a code, or an invite link (…/j/<code>). */
export function inviteCodeFrom(text: string): string | undefined {
  const value = text.trim();
  if (CODE.test(value)) return value.toUpperCase();
  const link = /\/j\/([A-Za-z0-9]{6,16})\/?(?:[?#].*)?$/.exec(value);
  return link?.[1]?.toUpperCase();
}

function JoinWithCode() {
  const [code, setCode] = useState('');
  const [error, setError] = useState(false);
  const submit = (event: FormEvent) => {
    event.preventDefault();
    const value = inviteCodeFrom(code);
    if (!value) {
      setError(true);
      return;
    }
    // Invite pages are server-rendered (they open the installed game).
    window.location.assign(`/j/${encodeURIComponent(value)}`);
  };
  return (
    <form className="join" onSubmit={submit} noValidate>
      <label>
        Got an invite code?
        <input
          name="code"
          value={code}
          onChange={(e) => {
            setCode(e.target.value);
            setError(false);
          }}
          placeholder="Code or invite link"
          autoComplete="off"
          autoCapitalize="characters"
          spellCheck={false}
          maxLength={200}
          aria-invalid={error}
          aria-describedby="join-hint"
        />
      </label>
      <button type="submit">Join</button>
      <p id="join-hint" className={`hint${error ? ' error' : ' caption'}`} aria-live="polite">
        {error
          ? 'Paste the invite link, or type its code (6 to 16 letters and digits).'
          : 'Friends share a code or a link from their room.'}
      </p>
    </form>
  );
}

function LiveStats() {
  const load = useLoad((signal) => api.stats(signal), []);
  const stats: InstanceStats | undefined = load.status === 'ready' ? load.data : undefined;
  if (load.status === 'error') return null;
  const value = (n: number | undefined) => (n === undefined ? '–' : n.toLocaleString());
  return (
    <ul className="stats" data-testid="live-stats" aria-busy={!stats}>
      <li className="stat">
        <span className="glob-icon" aria-hidden="true" />
        <span className="v">{value(stats?.playersOnline)}</span>
        <span className="k">players online</span>
      </li>
      <li className="stat">
        <GameArt name="warFlag" size={34} />
        <span className="v">{value(stats?.liveMatches)}</span>
        <span className="k">playing now</span>
      </li>
      <li className="stat">
        <GameArt name="swarm" size={34} />
        <span className="v">{value(stats?.matchesToday)}</span>
        <span className="k">matches today</span>
      </li>
    </ul>
  );
}

function Hero() {
  const { instance, instanceError } = useSession();
  const name = instance?.name ?? 'Globulation 2';
  const plainName = name === 'Globulation 2';
  return (
    <section className="hero" aria-labelledby="hero-title">
      <ColonyHero />
      <div className="hero-inner wrap">
        <div className="hero-card">
          <h1 id="hero-title">
            <Wordmark label={plainName ? null : 'Globulation 2'} />
            <span className={plainName ? 'sr-only' : 'instance'}>{name}</span>
          </h1>
          <p className="lede">
            Grow a colony of globs: feed them, school them, and send them exploring or to war. A
            free real-time strategy game where you set the goals and your colony gets to work.
          </p>
          {instanceError && (
            <div className="notice error" role="alert">
              The platform is unavailable: {instanceError.message}
            </div>
          )}
          <div className="hero-actions">
            <a className="btn primary big" href="/play/">
              <svg className="icon" viewBox="0 0 24 24" aria-hidden="true">
                <path d="M8 5.5v13l10.5-6.5z" fill="currentColor" />
              </svg>
              Play in browser
            </a>
            <a className="btn big" href={DOWNLOAD_URL} rel="noopener">
              <svg className="icon" viewBox="0 0 24 24" aria-hidden="true" fill="none">
                <path
                  d="M12 4v11m0 0-4.5-4.5M12 15l4.5-4.5M5 19.5h14"
                  stroke="currentColor"
                  strokeWidth="2.2"
                  strokeLinecap="round"
                  strokeLinejoin="round"
                />
              </svg>
              Download
            </a>
          </div>
          <JoinWithCode />
          <LiveStats />
          {instance && (
            <p className="caption" style={{ margin: 'var(--sp-3) 0 0' }}>
              {instance.guestsAllowed
                ? 'No account needed for rooms and casual games; sign in to be ranked.'
                : 'Sign in to play on this server.'}
            </p>
          )}
        </div>
      </div>
    </section>
  );
}

const FEATURES = [
  {
    art: 'swarm',
    title: 'Grow',
    text: 'Your swarm hatches globs. Raise inns, schools and racetracks; workers build and stock them on their own.',
  },
  {
    art: 'fruit',
    title: 'Feed',
    text: 'Globs gather wheat, wood and algae, and go hungry when inns run dry. A good economy wins long games.',
  },
  {
    art: 'warFlag',
    title: 'Explore and fight',
    text: 'Plant flags to send explorers scouting and warriors to battle. Out-think your rivals instead of out-clicking them.',
  },
] as const;

function LadderTeaser({ queue }: { queue: QueueInfo }) {
  const load = useLoad((signal) => api.leaderboard(queue.id, { limit: 5 }, signal), [queue.id]);
  return (
    <div className="card" data-testid="ladder-teaser">
      <div className="card-head">
        <h3>{queue.name}</h3>
        <Link to={`/leaderboard/${queue.id}`} className="caption">
          Full leaderboard
        </Link>
      </div>
      <Loaded load={load}>
        {(page) =>
          page.entries.length === 0 ? (
            <p className="muted">No rated players yet. The first wins will put you here.</p>
          ) : (
            <ol className="podium">
              {page.entries.map((e) => (
                <li key={e.rank}>
                  <span className="rank" aria-hidden="true">
                    {e.rank}
                  </span>
                  {e.entity.kind === 'account' ? (
                    <>
                      <Avatar account={e.entity.account} size="small" />
                      <span className="grow ell">
                        <PlayerLink account={e.entity.account} />
                      </span>
                    </>
                  ) : (
                    <span className="grow ell">{aiName(e.entity.ai)}</span>
                  )}
                  <span className="num">{rating(e.rating)}</span>
                </li>
              ))}
            </ol>
          )
        }
      </Loaded>
    </div>
  );
}

function FeaturedMaps() {
  const load = useLoad((signal) => api.maps({ sort: 'likes', limit: 4 }, signal), []);
  if (load.status !== 'ready' || load.data.items.length === 0) return null;
  return (
    <section aria-labelledby="maps-title">
      <div className="section-head">
        <h2 id="maps-title">Maps players love</h2>
        <Link to="/maps">Browse maps</Link>
      </div>
      <div className="map-grid" data-testid="featured-maps">
        {load.data.items.map((map: MapInfo) => {
          const v = map.latestVersion;
          return (
            <Link key={map.id} className="map-card" to={`/maps/${map.id}`}>
              <MapImage src={v?.previewUrl} alt="" />
              <span className="name ell">{map.title}</span>
              <span className="caption ell">
                {v?.teamCount ? `${v.teamCount} teams · ` : ''}by {map.owner.displayName} · ♥{' '}
                {map.stats.likes}
              </span>
            </Link>
          );
        })}
      </div>
    </section>
  );
}

function InstanceCard() {
  const { instance } = useSession();
  if (!instance) return null;
  return (
    <div className="card instance-card">
      <h3>This instance</h3>
      <dl>
        <dt>Address</dt>
        <dd>{instance.origin}</dd>
        <dt>Game versions</dt>
        <dd>
          {instance.supportedSimVersions.length === 0
            ? 'No game servers are online right now.'
            : instance.supportedSimVersions
                .map((v) => `format ${v.versionMinor}, network ${v.netProtocol}`)
                .join('; ')}
        </dd>
      </dl>
    </div>
  );
}

export function Home() {
  const { instance } = useSession();
  const recent = useLoad((signal) => api.matches({ limit: 6 }, signal), []);
  const rated = instance?.queues.filter((q) => q.rated) ?? [];
  return (
    <>
      <Hero />
      <div className="wrap home-body">
        <section aria-labelledby="features-title" className="features">
          <h2 id="features-title" className="sr-only">
            How a colony grows
          </h2>
          {FEATURES.map((f) => (
            <div className="feature" key={f.title}>
              <GameArt name={f.art} size={84} />
              <h3>{f.title}</h3>
              <p>{f.text}</p>
            </div>
          ))}
        </section>
        {rated.length > 0 && (
          <section aria-labelledby="ladders-title">
            <div className="section-head">
              <h2 id="ladders-title">Leaderboards</h2>
              <Link to="/leaderboard">All rankings</Link>
            </div>
            <div className="grid2">
              {rated.map((queue) => (
                <LadderTeaser key={queue.id} queue={queue} />
              ))}
            </div>
          </section>
        )}
        <FeaturedMaps />
        <section aria-labelledby="recent-title">
          <div className="section-head">
            <h2 id="recent-title">Recent matches</h2>
            <Link to="/matches">All matches</Link>
          </div>
          <Loaded load={recent}>{(page) => <MatchListView matches={page.items} />}</Loaded>
        </section>
        <div className="grid2" style={{ marginTop: 'var(--sp-6)' }}>
          <div className="card">
            <h3>Play anywhere</h3>
            <p className="muted" style={{ margin: 0 }}>
              The same game runs in your browser, on desktop and on phones. Invite links open
              whichever you have, and your account follows you.
            </p>
          </div>
          <InstanceCard />
        </div>
      </div>
    </>
  );
}
