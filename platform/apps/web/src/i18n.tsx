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
  aiTagLabel,
  musicMoodLabel,
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
  const parts = (
    singular !== undefined && count !== undefined ? tp(singular, source, count) : t(source)
  ).split(/(\{[A-Za-z][A-Za-z0-9_]*\})/g);
  const placeholder = (part: string) => /^\{[A-Za-z][A-Za-z0-9_]*\}$/.test(part);
  const valueOf = (part: string) => {
    const name = part.slice(1, -1);
    return Object.hasOwn(slots, name) ? slots[name] : part;
  };
  const result: ReactNode[] = [];
  for (let index = 0; index < parts.length; index++) {
    const part = parts[index] ?? '';
    if (!placeholder(part)) {
      result.push(part);
      continue;
    }
    const occurrence = occurrences.get(part) ?? 0;
    occurrences.set(part, occurrence + 1);
    const value = valueOf(part);
    // A whole numeric ratio/dimension/score needs one LTR isolate. Isolating its
    // operands separately still reverses numerator/denominator in RTL text.
    if (typeof value === 'number') {
      const numeric: ReactNode[] = [value];
      let last = index;
      while (last + 2 < parts.length) {
        const separator = parts[last + 1] ?? '';
        const token = parts[last + 2] ?? '';
        if (!/^\s*[/×–]\s*$/.test(separator) || !placeholder(token)) break;
        const next = valueOf(token);
        if (typeof next !== 'number') break;
        numeric.push(separator, next);
        occurrences.set(token, (occurrences.get(token) ?? 0) + 1);
        last += 2;
      }
      if (last > index) {
        result.push(
          <bdi dir="ltr" key={`${part}:${occurrence}`}>
            {numeric}
          </bdi>,
        );
        index = last;
        continue;
      }
    }
    const isolate =
      typeof value === 'string' &&
      (localization.locales.find((item) => item.code === localization.getLocale())?.dir === 'rtl' ||
        /[\u0590-\u08ff]/.test(value));
    result.push(
      <Fragment key={`${part}:${occurrence}`}>
        {isolate ? <bdi dir="auto">{value}</bdi> : value}
      </Fragment>,
    );
  }
  return <>{result}</>;
}
