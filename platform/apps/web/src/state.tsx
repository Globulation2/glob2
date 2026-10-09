import { useLocale } from './i18n.tsx';
// App-wide state: the instance description and the signed-in web session,
// plus a small hook for loading data that refetches when its key changes.
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
  type DependencyList,
  type ReactNode,
} from 'react';
import type { InstanceInfo, SelfAccount } from '@glob2/protocol';
import { ApiError, api, fetchInstance } from './api.ts';

export type Load<T> =
  { status: 'loading' } | { status: 'ready'; data: T } | { status: 'error'; error: Error };

/** Runs `load` when `deps` change, aborting the previous run. `reload` runs it again. */
export function useLoad<T>(
  load: (signal: AbortSignal) => Promise<T>,
  deps: DependencyList,
): Load<T> & { reload: () => void } {
  const [state, setState] = useState<Load<T>>({ status: 'loading' });
  const [tick, setTick] = useState(0);
  // A reload keeps showing the current data; new deps show a loading state.
  const reloading = useRef(false);
  useEffect(() => {
    const controller = new AbortController();
    if (!reloading.current) setState({ status: 'loading' });
    reloading.current = false;
    load(controller.signal).then(
      (data) => {
        if (!controller.signal.aborted) setState({ status: 'ready', data });
      },
      (error: unknown) => {
        if (!controller.signal.aborted) {
          setState({
            status: 'error',
            error: error instanceof Error ? error : new Error(String(error)),
          });
        }
      },
    );
    return () => controller.abort();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [...deps, tick]);
  const reload = useCallback(() => {
    reloading.current = true;
    setTick((t) => t + 1);
  }, []);
  return { ...state, reload };
}

interface Session {
  instance: InstanceInfo | undefined;
  instanceError: Error | undefined;
  /** undefined while loading; null when signed out. */
  account: SelfAccount | null | undefined;
  refresh: () => void;
  signOut: () => Promise<void>;
}

const SessionContext = createContext<Session | undefined>(undefined);

export function SessionProvider({ children }: { children: ReactNode }) {
  useLocale();
  const instance = useLoad((signal) => fetchInstance(signal), []);
  const me = useLoad(
    (signal) =>
      api.me(signal).catch((error: unknown) => {
        if (error instanceof ApiError && error.status === 401) return null;
        throw error;
      }),
    [],
  );
  const value: Session = {
    instance: instance.status === 'ready' ? instance.data : undefined,
    instanceError: instance.status === 'error' ? instance.error : undefined,
    account: me.status === 'ready' ? me.data : me.status === 'error' ? null : undefined,
    refresh: me.reload,
    async signOut() {
      await api.signOut();
      me.reload();
    },
  };
  return <SessionContext.Provider value={value}>{children}</SessionContext.Provider>;
}

export function useSession(): Session {
  const value = useContext(SessionContext);
  if (!value) throw new Error('useSession outside SessionProvider');
  return value;
}

export function isModerator(account: SelfAccount | null | undefined): boolean {
  return account?.role === 'moderator' || account?.role === 'admin';
}
