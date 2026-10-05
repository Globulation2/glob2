import { Players } from './pages/Players.tsx';
import { Skins } from './pages/Skins.tsx';
import { CommanderCredits } from './pages/Commander.tsx';
// The platform web app: home, leaderboards, player and match pages, the map
// catalog and moderation. Client routes the game links to must stay stable:
// /players/<id>, /matches/<id>, /maps/<id>, /leaderboard/<queueId>. Invite
// links (/j/<code>) and sign-in (/signin) are server-rendered by the API.
import { Suspense, lazy, useEffect, useRef, useState, type ReactNode } from 'react';
import { ART, GLOB_ICON, Wordmark, type ArtName } from './art.tsx';
import { Avatar, Loading } from './components/common.tsx';
import { DOWNLOAD_URL, Home, WEBSITE_URL, websitePage } from './pages/Home.tsx';
import { Leaderboard } from './pages/Leaderboard.tsx';
import { Matches } from './pages/Matches.tsx';
import { Link, RouterProvider, matchPath, useRouter } from './router.tsx';
import { SessionProvider, isModerator, useSession } from './state.tsx';
import { ThemeProvider, ThemeToggle } from './theme.tsx';

// Pages most visitors never open load on demand.
const Admin = lazy(() => import('./admin/Admin.tsx').then((m) => ({ default: m.Admin })));
const MapStudio = lazy(() =>
  import('./pages/MapStudio.tsx').then((m) => ({ default: m.MapStudio })),
);
const Maps = lazy(() => import('./pages/Maps.tsx').then((m) => ({ default: m.Maps })));
const MapPage = lazy(() => import('./pages/Maps.tsx').then((m) => ({ default: m.MapPage })));
const MapUpload = lazy(() => import('./pages/Maps.tsx').then((m) => ({ default: m.MapUpload })));
const Match = lazy(() => import('./pages/Match.tsx').then((m) => ({ default: m.Match })));
const AiPlayer = lazy(() => import('./pages/Player.tsx').then((m) => ({ default: m.AiPlayer })));
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
  { pattern: '/players', section: 'players', title: 'Players', render: () => <Players /> },
  {
    pattern: '/players/ai/:aiId',
    section: 'players',
    title: 'AI player',
    render: (p) => <AiPlayer key={p['aiId']} id={p['aiId'] ?? ''} />,
  },
  { pattern: '/skins', section: 'skins', title: 'Colony skins', render: () => <Skins /> },
  {
    pattern: '/map-studio',
    section: 'studio',
    title: 'AI Map Studio',
    render: () => <MapStudio />,
  },
  {
    pattern: '/map-studio/:id',
    section: 'studio',
    title: 'AI Map Studio',
    render: (p) => <MapStudio key={p['id']} id={p['id']} />,
  },
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

function About() {
  return (
    <details className="app-about">
      <summary>About & help</summary>
      <nav aria-label="About">
        <a href={DOWNLOAD_URL}>Download the game</a>
        {WEBSITE_URL && (
          <>
            <a href={WEBSITE_URL}>Globulation 2 Online website</a>
            <a href={websitePage('/learn/')}>Player guides</a>
            <a href={websitePage('/news/')}>News</a>
            <a href={websitePage('/community/')}>Community</a>
          </>
        )}
        <a href={SOURCE_URL}>Source code</a>
        <a href={CREDITS_URL}>Artwork and font credits</a>
      </nav>
    </details>
  );
}

function Layout() {
  const { location } = useRouter();
  const { instance, account } = useSession();
  const found = resolve(location.path);
  const section = found?.route.section;
  const name = instance?.name ?? 'Globulation 2';
  const home = section === 'home';
  const studio = section === 'studio';
  const main = useRef<HTMLElement>(null);
  const [collapsed, setCollapsed] = useState(false);
  const drawer = useRef<HTMLDialogElement>(null);
  const [drawerOpen, setDrawerOpen] = useState(false);
  const openNavigation = () => {
    setDrawerOpen(true);
    drawer.current?.showModal();
  };
  useEffect(() => {
    if (typeof window.matchMedia !== 'function') return;
    const close = () => drawer.current?.close();
    const expanded = window.matchMedia('(min-width: 1100px)');
    const mobile = window.matchMedia('(max-width: 899px)');
    expanded.addEventListener('change', close);
    mobile.addEventListener('change', close);
    return () => {
      expanded.removeEventListener('change', close);
      mobile.removeEventListener('change', close);
    };
  }, []);
  useEffect(() => {
    if (drawer.current?.open) drawer.current.close();
  }, [location.path]);
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
    { to: '/players', id: 'players', name: 'Players', art: 'school' },
    { to: '/matches', id: 'matches', name: 'Matches', art: 'swarm' },
    { to: '/maps', id: 'maps', name: 'Maps', art: 'explorationFlag' },
    { to: '/skins', id: 'skins', name: 'Skins', art: 'swarm' },
    { to: '/map-studio', id: 'studio', name: 'AI Map Studio', art: 'explorationFlag' },
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
  const navigation = (overlay: boolean) => (
    <>
      <div className="sidebar-brand">
        <Link className="brand" to="/" aria-label={`${name}, home`}>
          <img src={GLOB_ICON} width={34} height={34} alt="" />
          <Wordmark label={null} />
        </Link>
        {overlay ? (
          <button
            className="sidebar-close"
            onClick={() => drawer.current?.close()}
            aria-label="Close navigation"
          >
            ×
          </button>
        ) : (
          <button
            className="sidebar-toggle"
            onClick={() => setCollapsed(!collapsed)}
            aria-label={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
            aria-expanded={!collapsed}
          >
            ☰
          </button>
        )}
      </div>
      <a className="btn primary sidebar-play" href="/play/" aria-label="Play in browser">
        <span aria-hidden="true">▶</span>
        <span className="nav-label">Play in browser</span>
      </a>
      <nav className="nav" aria-label="Main">
        {['Play', 'Create', 'Manage'].map((group) => {
          const items = nav.filter((item) =>
            group === 'Play'
              ? ['home', 'leaderboard', 'players', 'matches'].includes(item.id)
              : group === 'Create'
                ? ['maps', 'skins', 'studio'].includes(item.id)
                : item.id === 'admin',
          );
          return items.length ? (
            <div className="nav-group" key={group}>
              <div className="nav-group-label">{group}</div>
              {items.map((item) => (
                <Link
                  key={item.id}
                  to={item.to}
                  className={section === item.id ? 'on' : ''}
                  aria-label={item.name}
                  aria-current={section === item.id ? 'page' : undefined}
                >
                  <img src={item.art ? ART[item.art] : GLOB_ICON} width={26} height={26} alt="" />
                  <span className="nav-label">{item.name}</span>
                  <span className="rail-tooltip" aria-hidden="true">
                    {item.name}
                  </span>
                </Link>
              ))}
            </div>
          ) : null;
        })}
      </nav>
      <div className="sidebar-end">
        <div className="sidebar-utilities">
          <About />
          <ThemeToggle />
        </div>
        <div
          className={`sidebar-account${section === 'account' || section === 'commander' ? ' on' : ''}`}
        >
          <AccountChip />
        </div>
      </div>
    </>
  );
  if (section === 'skins') return <Suspense fallback={<Loading />}>{page}</Suspense>;
  return (
    <div
      className={`site app-shell${home ? ' home' : ''}${studio ? ' studio-shell' : ''}${collapsed ? ' sidebar-collapsed' : ''}`}
    >
      <a className="skip-link" href="#main">
        Skip to content
      </a>
      <aside className="app-sidebar">{navigation(false)}</aside>
      <button
        className="rail-expand"
        onClick={() => {
          if (!studio && window.matchMedia('(min-width: 1100px)').matches) setCollapsed(false);
          else openNavigation();
        }}
        aria-label="Open navigation"
      >
        ☰
      </button>
      <header className="mobile-bar">
        <button onClick={openNavigation} aria-label="Open navigation">
          ☰
        </button>
        <Link to="/" aria-label={`${name}, home`}>
          <Wordmark label={null} />
        </Link>
      </header>
      <dialog
        ref={drawer}
        className="navigation-drawer"
        onClose={() => setDrawerOpen(false)}
        aria-label="Navigation"
        onClick={(event) => {
          if (event.target === event.currentTarget) drawer.current?.close();
        }}
      >
        {drawerOpen && <div className="drawer-content">{navigation(true)}</div>}
      </dialog>
      <main id="main" ref={main} tabIndex={-1}>
        <Suspense
          fallback={
            <div className="wrap">
              <Loading />
            </div>
          }
        >
          <div className="wrap">
            <div className="page">{page}</div>
          </div>
        </Suspense>
      </main>
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
