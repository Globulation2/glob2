// A small history-API router: the app has a handful of routes, all listed in
// routes.tsx. Paths the game links to (/players/<id>, /matches/<id>,
// /maps/<id>, /leaderboard/<queueId>) must keep working as deep links; the
// edge (deploy/Caddyfile) serves index.html for every non-API path.
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type AnchorHTMLAttributes,
  type MouseEvent,
  type ReactNode,
} from 'react';

export interface Location {
  path: string;
  search: URLSearchParams;
}

interface RouterValue {
  location: Location;
  navigate: (to: string, options?: { replace?: boolean }) => void;
}

const RouterContext = createContext<RouterValue | undefined>(undefined);

function current(): Location {
  return { path: window.location.pathname, search: new URLSearchParams(window.location.search) };
}

export function RouterProvider({ children }: { children: ReactNode }) {
  const [location, setLocation] = useState<Location>(current);
  useEffect(() => {
    const onPop = () => setLocation(current());
    window.addEventListener('popstate', onPop);
    return () => window.removeEventListener('popstate', onPop);
  }, []);
  const navigate = useCallback((to: string, options: { replace?: boolean } = {}) => {
    // Studio needs document-level COOP/COEP headers before embedding the threaded game.
    if (to.startsWith('/ai-studio') !== window.location.pathname.startsWith('/ai-studio')) {
      window.location.assign(to);
      return;
    }
    if (options.replace) window.history.replaceState(null, '', to);
    else window.history.pushState(null, '', to);
    setLocation(current());
    if (!options.replace) window.scrollTo(0, 0);
  }, []);
  const value = useMemo(() => ({ location, navigate }), [location, navigate]);
  return <RouterContext.Provider value={value}>{children}</RouterContext.Provider>;
}

export function useRouter(): RouterValue {
  const value = useContext(RouterContext);
  if (!value) throw new Error('useRouter outside RouterProvider');
  return value;
}

/** Matches `pattern` (segments, `:name` captures) against a path. */
export function matchPath(pattern: string, path: string): Record<string, string> | undefined {
  const want = pattern.split('/').filter(Boolean);
  const have = path.split('/').filter(Boolean);
  if (want.length !== have.length) return undefined;
  const params: Record<string, string> = {};
  for (let i = 0; i < want.length; i++) {
    const w = want[i] ?? '';
    let h: string;
    try {
      h = decodeURIComponent(have[i] ?? '');
    } catch {
      return undefined;
    }
    if (w.startsWith(':')) params[w.slice(1)] = h;
    else if (w !== h) return undefined;
  }
  return params;
}

type LinkProps = AnchorHTMLAttributes<HTMLAnchorElement> & { to: string };

/** An in-app link: plain clicks navigate without reloading. */
export function Link({ to, onClick, children, ...rest }: LinkProps) {
  const { navigate } = useRouter();
  const handle = (event: MouseEvent<HTMLAnchorElement>) => {
    onClick?.(event);
    if (
      event.defaultPrevented ||
      event.button !== 0 ||
      event.metaKey ||
      event.ctrlKey ||
      event.shiftKey ||
      event.altKey ||
      rest.target
    ) {
      return;
    }
    event.preventDefault();
    navigate(to);
  };
  return (
    <a href={to} onClick={handle} {...rest}>
      {children}
    </a>
  );
}
