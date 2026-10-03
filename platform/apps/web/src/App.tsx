import { CommanderCredits } from './pages/Commander.tsx';
// The platform web app: home, leaderboards, player and match pages, the map
// catalog and moderation. Client routes the game links to must stay stable:
// /players/<id>, /matches/<id>, /maps/<id>, /leaderboard/<queueId>. Invite
// links (/j/<code>) and sign-in (/signin) are server-rendered by the API.
import { Suspense, lazy, useEffect, useRef, type ReactNode } from 'react';
import { ART, GLOB_ICON, GameArt, Wordmark, type ArtName } from './art.tsx';
import { Avatar, Loading } from './components/common.tsx';
import { DOWNLOAD_URL, Home, WEBSITE_URL, websitePage } from './pages/Home.tsx';
import { Leaderboard } from './pages/Leaderboard.tsx';
import { Matches } from './pages/Matches.tsx';
import { Link, RouterProvider, matchPath, useRouter } from './router.tsx';
import { SessionProvider, isModerator, useSession } from './state.tsx';
import { ThemeProvider, ThemeToggle } from './theme.tsx';

// Pages most visitors never open load on demand.
const Admin = lazy(() => import('./admin/Admin.tsx').then((m) => ({ default: m.Admin })));
const Maps = lazy(() => import('./pages/Maps.tsx').then((m) => ({ default: m.Maps })));
const MapPage = lazy(() => import('./pages/Maps.tsx').then((m) => ({ default: m.MapPage })));
const MapUpload = lazy(() => import('./pages/Maps.tsx').then((m) => ({ default: m.MapUpload })));
const Match = lazy(() => import('./pages/Match.tsx').then((m) => ({ default: m.Match })));
const Player = lazy(() => import('./pages/Player.tsx').then((m) => ({ default: m.Player })));
const Account = lazy(() => import('./pages/Account.tsx').then((m) => ({ default: m.Account })));

/** Where the game's source and artwork credits live. */
const SOURCE_URL = 'https://github.com/Globulation2/glob2';
const CREDITS_URL = `${SOURCE_URL}/blob/master/docs/assets/source-attribution.md`;

interface Route {
  pattern: string;
  section: string;
  title: string;
  render: (params: Record<string, string>) => ReactNode;
}

export const ROUTES: Route[] = [
  {
    pattern: '/commander',
    section: 'commander',
    title: 'Hive Mind',
    render: () => <CommanderCredits />,
  },
  { pattern: '/', section: 'home', title: '', render: () => <Home /> },
  {
    pattern: '/leaderboard',
    section: 'leaderboard',
    title: 'Leaderboard',
    render: () => <Leaderboard queueId={undefined} />,
  },
  {
    pattern: '/leaderboard/:queueId',
    section: 'leaderboard',
    title: 'Leaderboard',
    render: (p) => <Leaderboard key={p['queueId']} queueId={p['queueId']} />,
  },
  {
    pattern: '/players/:id',
    section: 'players',
    title: 'Player',
    render: (p) => <Player key={p['id']} id={p['id'] ?? ''} />,
  },
  { pattern: '/matches', section: 'matches', title: 'Matches', render: () => <Matches /> },
  {
    pattern: '/matches/:id',
    section: 'matches',
    title: 'Match',
    render: (p) => <Match key={p['id']} id={p['id'] ?? ''} />,
  },
  { pattern: '/account', section: 'account', title: 'Your account', render: () => <Account /> },
  { pattern: '/maps', section: 'maps', title: 'Maps', render: () => <Maps mine={false} /> },
  { pattern: '/maps/mine', section: 'maps', title: 'My maps', render: () => <Maps mine /> },
  { pattern: '/maps/new', section: 'maps', title: 'Upload a map', render: () => <MapUpload /> },
  {
    pattern: '/maps/:id',
    section: 'maps',
    title: 'Map',
    render: (p) => <MapPage key={p['id']} id={p['id'] ?? ''} />,
  },
  {
    pattern: '/admin',
    section: 'admin',
    title: 'Moderation',
    render: () => <Admin tab={undefined} />,
  },
  {
    pattern: '/admin/:tab',
    section: 'admin',
    title: 'Moderation',
    render: (p) => <Admin tab={p['tab']} />,
  },
];

function resolve(path: string) {
  for (const route of ROUTES) {
    const params = matchPath(route.pattern, path);
    if (params) return { route, params };
  }
  return undefined;
}

function AccountChip() {
  const { account, signOut } = useSession();
  if (account === undefined) return null;
  if (!account) {
    return (
      <a className="btn primary small" href="/signin">
        Sign in
      </a>
    );
  }
  return (
    <>
      <Link className="chip" to="/account" data-testid="account-chip">
        <Avatar account={account} size="small" />
        <span>{account.displayName}</span>
      </Link>
      <button className="small" onClick={() => void signOut()}>
        Sign out
      </button>
    </>
  );
}

interface NavItem {
  to: string;
  id: string;
  name: string;
  art?: ArtName;
}

/**
 * The strip above the header that joins this app to the public website
 * (Globulation 2 Online): the website shows the same strip pointing here.
 * Only when the website is hosted apart from the app (VITE_WEBSITE_URL).
 */
function SiteBar() {
  if (!WEBSITE_URL) return null;
  return (
    <nav className="site-bar" aria-label="Globulation 2 Online website">
      <div className="wrap">
        <a href={WEBSITE_URL}>
          <span aria-hidden="true">←</span> Website
        </a>
        <a href={websitePage('/game/')}>The game</a>
        <a href={websitePage('/learn/')}>Learn</a>
        <a href={websitePage('/news/')}>News</a>
        <a href={DOWNLOAD_URL}>Downloads</a>
      </div>
    </nav>
  );
}

function Footer() {
  return (
    <footer className="site-footer">
      <div className="decor" aria-hidden="true">
        <GameArt name="wood" size={56} />
        <GameArt name="fruit" size={40} />
        <GameArt name="wood" size={44} />
      </div>
      <div className="wrap">
        <div>
          <Wordmark label={null} />
          <p>
            Globulation 2 is free software (GPL 3): a real-time strategy game where you lead a
            colony of globs by setting goals, not by clicking every unit.
          </p>
        </div>
        <nav aria-label="Play">
          <h2>Play</h2>
          <ul>
            <li>
              <a href="/play/">Play in browser</a>
            </li>
            <li>
              <a href={DOWNLOAD_URL} rel="noopener">
                Download the game
              </a>
            </li>
            <li>
              <Link to="/leaderboard">Leaderboards</Link>
            </li>
            <li>
              <Link to="/maps">Maps</Link>
            </li>
          </ul>
        </nav>
        <nav aria-label="About">
          <h2>About</h2>
          <ul>
            {WEBSITE_URL && (
              <>
                <li>
                  <a href={WEBSITE_URL}>Globulation 2 Online website</a>
                </li>
                <li>
                  <a href={websitePage('/learn/')}>Player guides</a>
                </li>
                <li>
                  <a href={websitePage('/news/')}>News</a>
                </li>
                <li>
                  <a href={websitePage('/community/')}>Community</a>
                </li>
              </>
            )}
            <li>
              <a href={SOURCE_URL} rel="noopener">
                Source code
              </a>
            </li>
            <li>
              <a href={CREDITS_URL} rel="noopener">
                Artwork and font credits
              </a>
            </li>
          </ul>
        </nav>
        <p className="fine">
          Pictures on this site are the game&rsquo;s own artwork by the Globulation 2 artists (GPL
          3). Fonts: Glob2 Sans (DejaVu) and Nunito (SIL Open Font License).
        </p>
      </div>
    </footer>
  );
}

function Layout() {
  const { location } = useRouter();
  const { instance, account } = useSession();
  const found = resolve(location.path);
  const section = found?.route.section;
  const name = instance?.name ?? 'Globulation 2';
  const home = section === 'home';
  const main = useRef<HTMLElement>(null);
  const top = useRef<HTMLDivElement>(null);
  // The home hero runs under the website strip and the header; it keeps its
  // content clear of them by their measured height (they wrap on phones).
  useEffect(() => {
    const element = top.current;
    if (!element || typeof ResizeObserver === 'undefined') return;
    const observer = new ResizeObserver(() => {
      document.documentElement.style.setProperty('--site-top-height', `${element.offsetHeight}px`);
    });
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  const first = useRef(true);
  useEffect(() => {
    const title = found ? found.route.title : 'Page not found';
    document.title = title ? `${title} · ${name}` : name;
  }, [found, name]);
  // After in-app navigation, move focus to the new page for keyboard and screen reader users.
  useEffect(() => {
    if (first.current) {
      first.current = false;
      return;
    }
    main.current?.focus({ preventScroll: true });
  }, [location.path]);
  const nav: NavItem[] = [
    { to: '/', id: 'home', name: 'Home' },
    { to: '/leaderboard', id: 'leaderboard', name: 'Leaderboard', art: 'warFlag' },
    { to: '/matches', id: 'matches', name: 'Matches', art: 'swarm' },
    { to: '/maps', id: 'maps', name: 'Maps', art: 'explorationFlag' },
    ...(isModerator(account)
      ? [{ to: '/admin', id: 'admin', name: 'Moderation', art: 'hospital' as ArtName }]
      : []),
  ];
  const page = found ? (
    found.route.render(found.params)
  ) : (
    <>
      <h1>Page not found</h1>
      <div className="notice">
        Nothing here: this page wandered off like an explorer glob.{' '}
        <Link to="/">Go to the home page</Link>.
      </div>
    </>
  );
  return (
    <div className={`site${home ? ' home' : ''}`}>
      <a className="skip-link" href="#main">
        Skip to content
      </a>
      <div className="world-band" aria-hidden="true" />
      <div className="site-top" ref={top}>
        <SiteBar />
        <header className="site-header">
          <div className="wrap">
            <Link className="brand" to="/" aria-label={`${name}, home`}>
              <img src={GLOB_ICON} width={34} height={34} alt="" />
              <Wordmark label={null} />
            </Link>
            <nav className="nav" aria-label="Main">
              {nav.map((item) => (
                <Link
                  key={item.id}
                  to={item.to}
                  className={section === item.id ? 'on' : ''}
                  aria-current={section === item.id ? 'page' : undefined}
                >
                  {item.art && <img src={ART[item.art]} width={26} height={26} alt="" />}
                  {item.name}
                </Link>
              ))}
            </nav>
            <div className="header-end">
              <ThemeToggle />
              <AccountChip />
            </div>
          </div>
        </header>
      </div>
      <main id="main" ref={main} tabIndex={-1}>
        <Suspense
          fallback={
            <div className="wrap">
              <Loading />
            </div>
          }
        >
          {home ? (
            page
          ) : (
            <div className="wrap">
              <div className="page">{page}</div>
            </div>
          )}
        </Suspense>
      </main>
      <Footer />
    </div>
  );
}

export function App() {
  return (
    <ThemeProvider>
      <RouterProvider>
        <SessionProvider>
          <Layout />
        </SessionProvider>
      </RouterProvider>
    </ThemeProvider>
  );
}
