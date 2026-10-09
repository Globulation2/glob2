import {
  Fragment,
  createContext,
  useContext,
  useEffect,
  useState,
  useSyncExternalStore,
  type ReactNode,
} from 'react';
import * as localization from '../../../packages/i18n/src/index.ts';

import { t, tp } from './messages.ts';
export {
  t,
  tp,
  translateError,
  getLocale,
  formatNumber,
  formatDate,
  formatRelative,
  message,
  displayMessage,
  MessageError,
  statusLabel,
  artifactLabel,
  fixedCaption,
} from './messages.ts';
const LocaleContext = createContext('en');
export function useLocale() {
  return useContext(LocaleContext);
}
export function LocaleProvider({ children }: { children: ReactNode }) {
  const locale = useSyncExternalStore(localization.subscribe, localization.getLocale, () => 'en');
  useEffect(() => {
    document.documentElement.lang = locale;
    document.documentElement.dir =
      localization.locales.find((item) => item.code === locale)?.dir ?? 'ltr';
  }, [locale]);
  return <LocaleContext.Provider value={locale}>{children}</LocaleContext.Provider>;
}
export function LanguageSelector() {
  const locale = useLocale();
  const [error, setError] = useState(false);
  return (
    <div className="language-preference">
      <label>
        {t('Language')}
        <select
          data-testid="language-selector"
          aria-label={t('Language')}
          value={locale}
          onChange={(event) => {
            setError(false);
            void localization.setLocale(event.target.value).catch(() => setError(true));
          }}
        >
          {localization.locales.map((item) => (
            <option key={item.code} value={item.code} lang={item.code}>
              {item.name}
            </option>
          ))}
        </select>
      </label>
      {error && <p role="alert">{t('Could not load this language. Please try again.')}</p>}
    </div>
  );
}

/** Named React slots let translations reorder links, emphasis and interpolated values. */
export function RichMessage({
  source,
  slots,
  singular,
  count,
}: {
  source: string;
  slots: Record<string, ReactNode>;
  singular?: string;
  count?: number;
}) {
  useLocale();
  const occurrences = new Map<string, number>();
  return (
    <>
      {(singular !== undefined && count !== undefined ? tp(singular, source, count) : t(source))
        .split(/(\{[A-Za-z][A-Za-z0-9_]*\})/g)
        .map((part) => {
          if (!/^\{[A-Za-z][A-Za-z0-9_]*\}$/.test(part)) return part;
          const occurrence = occurrences.get(part) ?? 0;
          occurrences.set(part, occurrence + 1);
          const name = part.slice(1, -1);
          const value = Object.hasOwn(slots, name) ? (slots[name] ?? part) : part;
          const isolate =
            typeof value === 'string' &&
            (localization.locales.find((item) => item.code === localization.getLocale())?.dir ===
              'rtl' ||
              /[\u0590-\u08ff]/.test(value));
          return (
            <Fragment key={`${part}:${occurrence}`}>
              {isolate ? <bdi dir="auto">{value}</bdi> : value}
            </Fragment>
          );
        })}
    </>
  );
}
