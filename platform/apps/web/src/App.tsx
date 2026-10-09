import { Generators, GeneratorPage, GeneratorPublish } from './pages/Generators.tsx';
import { studioLocal } from './components/studio/storage.ts';
import { Players } from './pages/Players.tsx';
import './music/music.css';
import { Skins } from './pages/Skins.tsx';
import { CommanderCredits } from './pages/Commander.tsx';
// The platform web app: home, leaderboards, player and match pages, the map
// catalog and moderation. Client routes the game links to must stay stable:
// /players/<id>, /matches/<id>, /maps/<id>, /leaderboard/<queueId>. Invite
// links (/j/<code>) and sign-in (/signin) are server-rendered by the API.
import { Suspense, lazy, useEffect, useRef, useState, type ReactNode } from 'react';
import { GLOB_ICON, Wordmark } from './art.tsx';
import { Icon, type IconName } from './icons.tsx';
import { Avatar, Loading } from './components/common.tsx';
import { DOWNLOAD_URL, Home, WEBSITE_URL, websitePage } from './pages/Home.tsx';
import { Leaderboard } from './pages/Leaderboard.tsx';
import { Matches } from './pages/Matches.tsx';
import { Link, RouterProvider, matchPath, useRouter } from './router.tsx';
import { SessionProvider, isModerator, useSession } from './state.tsx';
import { ThemeProvider, ThemeToggle } from './theme.tsx';

// Pages most visitors never open load on demand.
const AiBuildingStudio = lazy(() =>
  import('./pages/AiBuildingStudio.tsx').then((m) => ({ default: m.AiBuildingStudio })),
);
const TerrainStudio = lazy(() =>
  import('./pages/TerrainStudio.tsx').then((m) => ({ default: m.TerrainStudio })),
);
const MusicLibrary = lazy(() =>
  import('./music/Library.tsx').then((m) => ({ default: m.MusicLibrary })),
);
const MusicDetail = lazy(() =>
  import('./music/Library.tsx').then((m) => ({ default: m.MusicDetail })),
);
const MusicCreate = lazy(() =>
  import('./music/Library.tsx').then((m) => ({ default: m.MusicCreate })),
);
const Admin = lazy(() => import('./admin/Admin.tsx').then((m) => ({ default: m.Admin })));
const MusicStudio = lazy(() =>
  import('./pages/MusicStudio.tsx').then((m) => ({ default: m.MusicStudio })),
);
const MapStudio = lazy(() =>
  import('./pages/MapStudio.tsx').then((m) => ({ default: m.MapStudio })),
);
const GeneratorStudio = lazy(() =>
  import('./pages/GeneratorStudio.tsx').then((m) => ({ default: m.GeneratorStudio })),
);
const AiStudio = lazy(() => import('./pages/AiStudio.tsx').then((m) => ({ default: m.AiStudio })));
const BuildingLibrary = lazy(() =>
  import('./pages/BuildingLibrary.tsx').then((m) => ({ default: m.BuildingLibrary })),
);
const BuildingStudio = lazy(() =>
  import('./pages/BuildingStudio.tsx').then((m) => ({ default: m.BuildingStudio })),
);
const Ais = lazy(() => import('./pages/Ais.tsx').then((m) => ({ default: m.Ais })));
const AiPage = lazy(() => import('./pages/Ais.tsx').then((m) => ({ default: m.AiPage })));
const AiPublish = lazy(() => import('./pages/Ais.tsx').then((m) => ({ default: m.AiPublish })));
const SetLibrary = lazy(() =>
  import('./sets/Library.tsx').then((m) => ({ default: m.SetLibrary })),
);
const SetReports = lazy(() =>
  import('./sets/Library.tsx').then((m) => ({ default: m.SetReports })),
);
const SetDetail = lazy(() => import('./sets/Library.tsx').then((m) => ({ default: m.SetDetail })));
const SetWorkspace = lazy(() =>
  import('./sets/Workspace.tsx').then((m) => ({ default: m.SetWorkspace })),
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
  workspace?: boolean;
  section: string;
  title: string;
  render: (params: Record<string, string>) => ReactNode;
}

export const ROUTES: Route[] = [
  {
    pattern: '/buildings',
    section: 'buildings',
    title: 'Building library',
    render: () => <BuildingLibrary />,
  },
  {
    pattern: '/buildings/:id',
    section: 'buildings',
    title: 'Building family',
    render: (p) => <BuildingLibrary key={p['id']} id={p['id']} />,
  },
  {
    pattern: '/ai-building-studio',
    workspace: true,
    section: 'buildings',
    title: 'AI Building Studio',
    render: () => <AiBuildingStudio />,
  },
  {
    pattern: '/ai-building-studio/:id',
    workspace: true,
    section: 'buildings',
    title: 'AI Building Studio',
    render: (p) => <AiBuildingStudio key={p['id']} id={p['id']} />,
  },
  {
    pattern: '/building-studio',
    section: 'buildings',
    title: 'Building Studio',
    render: () => <BuildingStudio />,
  },
  {
    pattern: '/building-studio/:id',
    section: 'buildings',
    title: 'Building Studio',
    render: (p) => <BuildingStudio key={p['id']} id={p['id']} />,
  },
  {
    pattern: '/sets',
    section: 'sets',
    title: 'Terrain & resource sets',
    render: () => <SetLibrary />,
  },
  {
    pattern: '/terrain-studio',
    workspace: true,
    section: 'sets',
    title: 'AI Terrain Studio',
    render: () => <TerrainStudio />,
  },
  {
    pattern: '/terrain-studio/:id',
    workspace: true,
    section: 'sets',
    title: 'AI Terrain Studio',
    render: (p) => <TerrainStudio key={p['id']} id={p['id']} />,
  },
  { pattern: '/sets/mine', section: 'sets', title: 'My sets', render: () => <SetLibrary mine /> },
  { pattern: '/sets/new', section: 'sets', title: 'Create a set', render: () => <SetWorkspace /> },
  {
    pattern: '/sets/drafts/:id',
    section: 'sets',
    title: 'Set workspace',
    render: (p) => <SetWorkspace key={p['id']} id={p['id']} />,
  },
  { pattern: '/sets/reports', section: 'sets', title: 'Set reports', render: () => <SetReports /> },
  {
    pattern: '/sets/:id',
    section: 'sets',
    title: 'Set library',
    render: (p) => <SetDetail key={p['id']} id={p['id'] ?? ''} />,
  },
  {
    pattern: '/ai-studio',
    workspace: true,
    section: 'ais',
    title: 'AI Studio',
    render: () => <AiStudio />,
  },
  {
    pattern: '/ai-studio/:id',
    workspace: true,
    section: 'ais',
    title: 'AI Studio',
    render: (p) => <AiStudio key={p['id']} id={p['id']} />,
  },
  {
    pattern: '/generator-studio',
    workspace: true,
    section: 'maps',
    title: 'Generator Studio',
    render: () => <GeneratorStudio />,
  },
  {
    pattern: '/generator-studio/:id',
    workspace: true,
    section: 'maps',
    title: 'Generator Studio',
    render: (p) => <GeneratorStudio key={p['id']} id={p['id']} />,
  },
  {
    pattern: '/generators',
    section: 'maps',
    title: 'Map generators',
    render: () => <Generators />,
  },
  {
    pattern: '/generators/mine',
    section: 'maps',
    title: 'My generators',
    render: () => <Generators mine />,
  },
  {
    pattern: '/generators/new',
    section: 'maps',
    title: 'Share a generator',
    render: () => <GeneratorPublish />,
  },
  {
    pattern: '/generators/:id/new',
    section: 'maps',
    title: 'Publish a release',
    render: (p) => <GeneratorPublish id={p['id'] ?? ''} />,
  },
  {
    pattern: '/generators/:id',
    section: 'maps',
    title: 'Map generator',
    render: (p) => <GeneratorPage id={p['id'] ?? ''} />,
  },
  { pattern: '/ais', section: 'ais', title: 'AI Library', render: () => <Ais /> },
  { pattern: '/ais/mine', section: 'ais', title: 'My AIs', render: () => <Ais view="mine" /> },
  {
    pattern: '/ais/favourites',
    section: 'ais',
    title: 'Favourite AIs',
    render: () => <Ais view="favourites" />,
  },
  { pattern: '/ais/new', section: 'ais', title: 'Share your AI', render: () => <AiPublish /> },
  {
    pattern: '/ais/:id/new',
    section: 'ais',
    title: 'New AI version',
    render: (p) => <AiPublish key={p['id']} id={p['id'] ?? ''} />,
  },
  {
    pattern: '/ais/:id',
    section: 'ais',
    title: 'AI Library',
    render: (p) => <AiPage key={p['id']} id={p['id'] ?? ''} />,
  },
  { pattern: '/players', section: 'players', title: 'Players', render: () => <Players /> },
  {
    pattern: '/players/ai/:aiId',
    section: 'players',
    title: 'AI player',
    render: (p) => <AiPlayer key={p['aiId']} id={p['aiId'] ?? ''} />,
  },
  {
    pattern: '/music-studio',
    workspace: true,
    section: 'music',
    title: 'AI Music Studio',
    render: () => <MusicStudio />,
  },
  {
    pattern: '/music-studio/:id',
    workspace: true,
    section: 'music',
    title: 'AI Music Studio',
    render: (p) => <MusicStudio key={p['id']} id={p['id']} />,
  },
  { pattern: '/music', section: 'music', title: 'Music', render: () => <MusicLibrary /> },
  { pattern: '/music/new', section: 'music', title: 'Share music', render: () => <MusicCreate /> },
  {
    pattern: '/music/:id',
    section: 'music',
    title: 'Music',
    render: (p) => <MusicDetail key={p['id']} id={p['id'] ?? ''} />,
  },
  { pattern: '/skins', section: 'skins', title: 'Colony skins', render: () => <Skins /> },
  {
    pattern: '/map-studio',
    workspace: true,
    section: 'maps',
    title: 'AI Map Studio',
    render: () => <MapStudio />,
  },
  {
    pattern: '/map-studio/:id',
    workspace: true,
    section: 'maps',
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
      <Link
        className="chip"
        to="/account"
        data-testid="account-chip"
        aria-label={account.displayName}
      >
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
  icon?: IconName;
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
  const studio = section === 'skins' || !!found?.route.workspace;
  const main = useRef<HTMLElement>(null);
  const navigationKey = `studio-navigation:${account?.id ?? 'anonymous'}`;
  const [navigationPreference, setNavigationPreference] = useState(() => ({
    key: navigationKey,
    collapsed: studioLocal.getItem(navigationKey) !== 'expanded',
  }));
  if (navigationPreference.key !== navigationKey)
    setNavigationPreference({
      key: navigationKey,
      collapsed: studioLocal.getItem(navigationKey) !== 'expanded',
    });
  const collapsed =
    studio &&
    (navigationPreference.key === navigationKey
      ? navigationPreference.collapsed
      : studioLocal.getItem(navigationKey) !== 'expanded');
  const setCollapsed = (value: boolean) => {
    setNavigationPreference({ key: navigationKey, collapsed: value });
    studioLocal.setItem(navigationKey, value ? 'collapsed' : 'expanded');
  };
  const drawer = useRef<HTMLDialogElement>(null);
  const [drawerOpen, setDrawerOpen] = useState(false);
  const openNavigation = () => {
    setDrawerOpen(true);
    drawer.current?.showModal();
  };
  useEffect(() => {
    if (typeof window.matchMedia !== 'function') return;
    const close = () => drawer.current?.close();
    const rail = window.matchMedia(studio ? '(min-width: 600px)' : '(min-width: 900px)');
    rail.addEventListener('change', close);
    return () => rail.removeEventListener('change', close);
  }, [studio]);
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
    { to: '/leaderboard', id: 'leaderboard', name: 'Leaderboard', icon: 'trophy' },
    { to: '/players', id: 'players', name: 'Players', icon: 'users' },
    { to: '/matches', id: 'matches', name: 'Matches', icon: 'swords' },
    { to: '/maps', id: 'maps', name: 'Maps', icon: 'map' },
    { to: '/sets', id: 'sets', name: 'Terrain & resources', icon: 'palette' },
    { to: '/ais', id: 'ais', name: 'AI Library', icon: 'robot' },
    { to: '/buildings', id: 'buildings', name: 'Buildings', icon: 'map' },
    { to: '/music', id: 'music', name: 'Music', icon: 'music' },
    { to: '/skins', id: 'skins', name: 'Skins', icon: 'palette' },
    ...(isModerator(account)
      ? [{ to: '/admin', id: 'admin', name: 'Moderation', icon: 'shield-check' as IconName }]
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
            <Icon name="x" />
          </button>
        ) : (
          <button
            className="sidebar-toggle"
            onClick={() => setCollapsed(!collapsed)}
            aria-label={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
            aria-expanded={!collapsed}
          >
            <Icon name="list-details" />
          </button>
        )}
      </div>
      <a className="btn primary sidebar-play" href="/play/" aria-label="Play in browser">
        <Icon name="player-play" size={20} />
        <span className="nav-label">Play in browser</span>
      </a>
      <nav className="nav" aria-label="Main">
        {['Play', 'Create', 'Manage'].map((group) => {
          const items = nav.filter((item) =>
            group === 'Play'
              ? ['home', 'leaderboard', 'players', 'matches'].includes(item.id)
              : group === 'Create'
                ? ['maps', 'sets', 'ais', 'buildings', 'music', 'skins'].includes(item.id)
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
                  {item.icon ? (
                    <span className={`nav-chip nav-chip-${item.id}`}>
                      <Icon name={item.icon} size={20} />
                    </span>
                  ) : (
                    <img src={GLOB_ICON} width={26} height={26} alt="" />
                  )}
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
          if (window.matchMedia('(min-width: 1100px)').matches) setCollapsed(false);
          else openNavigation();
        }}
        aria-label="Open navigation"
      >
        <Icon name="list-details" />
      </button>
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
