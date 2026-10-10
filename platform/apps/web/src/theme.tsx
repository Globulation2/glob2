import { useEffect } from 'react';
import { getThemeController } from '@glob2/design-system';
import { t, useLocale } from './i18n.tsx';
import { ThemeToggle as SharedThemeToggle, useTheme } from '@glob2/design-system/react';
export { ThemeProvider, useTheme } from '@glob2/design-system/react';
export type { Theme, ThemePreference } from '@glob2/design-system';

/** SPA routes do not rerun the document bootstrap, so refresh on navigation. */
export function useThemeNavigation(navigationKey: string) {
  useEffect(() => {
    getThemeController()?.refresh();
  }, [navigationKey]);
}

/** Application translation stays here; persistence and interaction are shared. */
export function ThemeToggle() {
  useLocale();
  const { preference } = useTheme();
  const labels = {
    system: t('Theme: same as this device'),
    light: t('Theme: light (meadow)'),
    dark: t('Theme: dark (night colony)'),
  };
  return (
    <SharedThemeToggle
      data-testid="theme-toggle"
      aria-label={t(`${labels[preference]}. Change theme`)}
      labels={{ ...labels, change: '' }}
    />
  );
}
