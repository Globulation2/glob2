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
  useRef,
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
  const historyIndex = useRef<number>(window.history.state?.glob2HistoryIndex ?? 0);
  const restoringHistory = useRef(false);
  useEffect(() => {
    window.history.replaceState(
      { ...window.history.state, glob2HistoryIndex: historyIndex.current },
      '',
      window.location.href,
    );
    const onPop = (event: PopStateEvent) => {
      const target =
        typeof event.state?.glob2HistoryIndex === 'number'
          ? event.state.glob2HistoryIndex
          : historyIndex.current - 1;
      // A cancelled Back/Forward already moved the browser URL. Restore its original
      // history entry without unmounting the editor or prompting a second time.
      if (restoringHistory.current) {
        restoringHistory.current = false;
        return;
      }
      if (!window.dispatchEvent(new Event('glob2-before-navigate', { cancelable: true }))) {
        restoringHistory.current = true;
        window.history.go(historyIndex.current - target);
        return;
      }
      historyIndex.current = target;
      setLocation(current());
    };
    window.addEventListener('popstate', onPop);
    return () => window.removeEventListener('popstate', onPop);
  }, []);
  const navigate = useCallback((to: string, options: { replace?: boolean } = {}) => {
    if (
      !options.replace &&
      !window.dispatchEvent(new Event('glob2-before-navigate', { cancelable: true }))
    )
      return;
    // Studio needs document-level COOP/COEP headers before embedding the threaded game.
    if (
      /^\/(ai-studio|generator-studio)(\/|$)/.test(to) !==
      /^\/(ai-studio|generator-studio)(\/|$)/.test(window.location.pathname)
    ) {
      window.location.assign(to);
      return;
    }
    if (!options.replace) historyIndex.current++;
    const state = { ...window.history.state, glob2HistoryIndex: historyIndex.current };
    if (options.replace) window.history.replaceState(state, '', to);
    else window.history.pushState(state, '', to);
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
