// The platform web app: home, leaderboards, player and match pages, the map
// catalog and moderation. Client routes the game links to must stay stable:
// /players/<id>, /matches/<id>, /maps/<id>, /leaderboard/<queueId>. Invite
// links (/j/<code>) and sign-in (/signin) are server-rendered by the API.
import { useEffect, type ReactNode } from 'react';
import { Admin } from './admin/Admin.tsx';
import { initial } from './format.ts';
import { Home } from './pages/Home.tsx';
import { Leaderboard } from './pages/Leaderboard.tsx';
import { MapPage, MapUpload, Maps } from './pages/Maps.tsx';
import { Match } from './pages/Match.tsx';
import { Matches } from './pages/Matches.tsx';
import { Player } from './pages/Player.tsx';
import { Link, RouterProvider, matchPath, useRouter } from './router.tsx';
import { SessionProvider, isModerator, useSession } from './state.tsx';

interface Route {
  pattern: string;
  section: string;
  title: string;
  render: (params: Record<string, string>) => ReactNode;
}

export const ROUTES: Route[] = [
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
    <span style={{ display: 'inline-flex', gap: 6, alignItems: 'center' }}>
      <Link className="chip" to={`/players/${account.id}`} data-testid="account-chip">
        <span className="avatar small" aria-hidden="true">
          {initial(account.displayName)}
        </span>
        {account.displayName}
      </Link>
      <button className="small" onClick={() => void signOut()}>
        Sign out
      </button>
    </span>
  );
}

function Layout() {
  const { location } = useRouter();
  const { instance, account } = useSession();
  const found = resolve(location.path);
  const section = found?.route.section;
  const name = instance?.name ?? 'Globulation 2';
  useEffect(() => {
    document.title = found?.route.title ? `${found.route.title} · ${name}` : name;
  }, [found?.route.title, name]);
  const nav = [
    { to: '/', id: 'home', name: 'Home' },
    { to: '/leaderboard', id: 'leaderboard', name: 'Leaderboard' },
    { to: '/matches', id: 'matches', name: 'Matches' },
    { to: '/maps', id: 'maps', name: 'Maps' },
    ...(isModerator(account) ? [{ to: '/admin', id: 'admin', name: 'Moderation' }] : []),
  ];
  return (
    <div className="shell">
      <header className="topbar">
        <Link className="brand" to="/">
          {name}
        </Link>
        <AccountChip />
        <nav aria-label="Main">
          {nav.map((item) => (
            <Link
              key={item.id}
              to={item.to}
              className={section === item.id ? 'on' : ''}
              aria-current={section === item.id ? 'page' : undefined}
            >
              {item.name}
            </Link>
          ))}
        </nav>
      </header>
      <main className="page">
        {found ? (
          found.route.render(found.params)
        ) : (
          <div className="notice">
            Nothing here. <Link to="/">Go to the home page</Link>.
          </div>
        )}
      </main>
      <footer className="site">
        Globulation 2 is free software (GPL 3). <a href="/play/">Play in browser</a>
      </footer>
    </div>
  );
}

export function App() {
  return (
    <RouterProvider>
      <SessionProvider>
        <Layout />
      </SessionProvider>
    </RouterProvider>
  );
}
