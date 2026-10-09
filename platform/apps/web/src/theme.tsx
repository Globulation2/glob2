import { t, useLocale } from './i18n.tsx';
// Light ("Meadow") and dark ("Night colony") themes. The page follows the
// system setting unless the viewer picks one; the choice is stored in this
// browser only. index.html applies a stored choice before the first paint.
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from 'react';

export type ThemePreference = 'system' | 'light' | 'dark';
export type Theme = 'light' | 'dark';

const KEY = 'glob2-theme';

function stored(): ThemePreference {
  try {
    const value = window.localStorage.getItem(KEY);
    return value === 'light' || value === 'dark' ? value : 'system';
  } catch {
    return 'system';
  }
}

function systemDark(): MediaQueryList | undefined {
  return typeof window.matchMedia === 'function'
    ? window.matchMedia('(prefers-color-scheme: dark)')
    : undefined;
}

interface ThemeValue {
  preference: ThemePreference;
  theme: Theme;
  setPreference: (preference: ThemePreference) => void;
}

const ThemeContext = createContext<ThemeValue>({
  preference: 'system',
  theme: 'light',
  setPreference: () => undefined,
});

export function ThemeProvider({ children }: { children: ReactNode }) {
  useLocale();
  const [preference, setPreferenceState] = useState<ThemePreference>(stored);
  const [dark, setDark] = useState(() => systemDark()?.matches ?? false);
  useEffect(() => {
    const query = systemDark();
    if (!query) return;
    const onChange = () => setDark(query.matches);
    query.addEventListener('change', onChange);
    return () => query.removeEventListener('change', onChange);
  }, []);
  useEffect(() => {
    const root = document.documentElement;
    if (preference === 'system') root.removeAttribute('data-theme');
    else root.setAttribute('data-theme', preference);
  }, [preference]);
  const setPreference = useCallback((next: ThemePreference) => {
    setPreferenceState(next);
    try {
      if (next === 'system') window.localStorage.removeItem(KEY);
      else window.localStorage.setItem(KEY, next);
    } catch {
      // Storage may be blocked; the choice then lasts for this page only.
    }
  }, []);
  const theme: Theme = preference === 'system' ? (dark ? 'dark' : 'light') : preference;
  const value = useMemo(
    () => ({ preference, theme, setPreference }),
    [preference, theme, setPreference],
  );
  return <ThemeContext.Provider value={value}>{children}</ThemeContext.Provider>;
}

export function useTheme(): ThemeValue {
  return useContext(ThemeContext);
}

const NEXT: Record<ThemePreference, ThemePreference> = {
  system: 'light',
  light: 'dark',
  dark: 'system',
};
const LABEL: Record<ThemePreference, string> = {
  system: 'Theme: same as this device',
  light: 'Theme: light (meadow)',
  dark: 'Theme: dark (night colony)',
};

/** Cycles system → light → dark. */
export function ThemeToggle() {
  useLocale();
  const { preference, setPreference } = useTheme();
  return (
    <button
      type="button"
      className="theme-toggle"
      onClick={() => setPreference(NEXT[preference])}
      aria-label={t('{value0}. Change theme', { value0: t(LABEL[preference]) })}
      title={t(LABEL[preference])}
      data-testid="theme-toggle"
    >
      <svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="2">
        {preference === 'light' ? (
          <>
            <circle cx="12" cy="12" r="4.5" fill="currentColor" stroke="none" />
            <path
              d="M12 2.5v2.2M12 19.3v2.2M4.6 4.6l1.6 1.6M17.8 17.8l1.6 1.6M2.5 12h2.2M19.3 12h2.2M4.6 19.4l1.6-1.6M17.8 6.2l1.6-1.6"
              strokeLinecap="round"
            />
          </>
        ) : preference === 'dark' ? (
          <path
            d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z"
            fill="currentColor"
            stroke="none"
          />
        ) : (
          <>
            <circle cx="12" cy="12" r="8.5" />
            <path d="M12 3.5a8.5 8.5 0 0 1 0 17z" fill="currentColor" stroke="none" />
          </>
        )}
      </svg>
    </button>
  );
}
