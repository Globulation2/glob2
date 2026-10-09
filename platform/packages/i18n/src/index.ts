import { locales, type Locale } from './locales.ts';
import english from '../locales/en.json' with { type: 'json' };
export { locales, type Locale } from './locales.ts';
export type MessageKey = keyof typeof english;
export type Params = Record<string, string | number>;
export type Message = string | Partial<Record<Intl.LDMLPluralRule, string>>;
export type Catalog = Record<string, Message>;
const catalogs = new Map<string, Catalog>([['en', english as Catalog]]);
let initialization: Promise<void> | undefined;
let selected: Locale = 'en';
let revision = 0;
const listeners = new Set<() => void>();
const pending = new Map<string, Promise<void>>();
let customLoader: ((locale: Locale) => Promise<Catalog>) | undefined;
export function configureCatalogLoader(loader: (locale: Locale) => Promise<Catalog>): void {
  customLoader = loader;
  pending.clear();
}
interface BrowserScope {
  document?: { cookie: string; documentElement: { lang: string; dir: string } };
  navigator?: { languages?: readonly string[]; language?: string };
  dispatchEvent?: (event: unknown) => boolean;
  CustomEvent?: new (name: string) => unknown;
}
const scope = globalThis as unknown as BrowserScope;

/** Browser tags are ISO tags, not the game's historical catalog aliases. */
export function resolveLocale(input: string | readonly string[]): Locale {
  const preferences =
    typeof input === 'string'
      ? input
          .split(',')
          .map((part) => {
            const [tag, ...options] = part.trim().split(';');
            return {
              tag: tag ?? '',
              weight: Number(
                options
                  .find((o) => o.trim().startsWith('q='))
                  ?.trim()
                  .slice(2) ?? 1,
              ),
            };
          })
          .filter((p) => p.weight > 0)
          .sort((a, b) => b.weight - a.weight)
          .map((p) => p.tag)
      : input;
  for (const preference of preferences) {
    const tag = preference.toLowerCase().replaceAll('_', '-');
    const exact = locales.find((l) => l.code.toLowerCase() === tag);
    if (exact) return exact.code;
    const [language, ...parts] = tag.split('-');
    if (language === 'pt') return parts.includes('br') ? 'pt-BR' : 'pt-PT';
    if (language === 'zh')
      return parts.some((p) => ['hant', 'tw', 'hk', 'mo'].includes(p)) ? 'zh-Hant' : 'zh-Hans';
    if (language === 'sr') return 'sr-Cyrl';
    if (language === 'in') return 'id';
    const base = locales.find((l) => l.code === language);
    if (base) return base.code;
  }
  return 'en';
}
export function localeFromCookie(cookie: string): Locale | undefined {
  const value = cookie
    .split(';')
    .find((part) => part.trim().startsWith('glob2_locale='))
    ?.trim()
    .slice(13);
  if (!value) return undefined;
  try {
    return locales.find((l) => l.code === decodeURIComponent(value))?.code;
  } catch {
    return undefined;
  }
}
export function getLocale(): Locale {
  return selected;
}
export function subscribe(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
export function registerCatalog(locale: string, catalog: Catalog): void {
  catalogs.set(locale, catalog);
}
export function hasMessage(source: string, locale = selected): boolean {
  const catalog = catalogs.get(locale);
  return !!catalog && Object.hasOwn(catalog, source);
}
const loaders: Record<string, () => Promise<{ default: Catalog }>> = {
  ar: () => import('../locales/ar.json', { with: { type: 'json' } }),
  ca: () => import('../locales/ca.json', { with: { type: 'json' } }),
  cs: () => import('../locales/cs.json', { with: { type: 'json' } }),
  da: () => import('../locales/da.json', { with: { type: 'json' } }),
  de: () => import('../locales/de.json', { with: { type: 'json' } }),
  el: () => import('../locales/el.json', { with: { type: 'json' } }),
  en: () => import('../locales/en.json', { with: { type: 'json' } }),
  eo: () => import('../locales/eo.json', { with: { type: 'json' } }),
  es: () => import('../locales/es.json', { with: { type: 'json' } }),
  eu: () => import('../locales/eu.json', { with: { type: 'json' } }),
  fa: () => import('../locales/fa.json', { with: { type: 'json' } }),
  fi: () => import('../locales/fi.json', { with: { type: 'json' } }),
  fr: () => import('../locales/fr.json', { with: { type: 'json' } }),
  hu: () => import('../locales/hu.json', { with: { type: 'json' } }),
  id: () => import('../locales/id.json', { with: { type: 'json' } }),
  it: () => import('../locales/it.json', { with: { type: 'json' } }),
  ja: () => import('../locales/ja.json', { with: { type: 'json' } }),
  ko: () => import('../locales/ko.json', { with: { type: 'json' } }),
  nl: () => import('../locales/nl.json', { with: { type: 'json' } }),
  pl: () => import('../locales/pl.json', { with: { type: 'json' } }),
  'pt-BR': () => import('../locales/pt-BR.json', { with: { type: 'json' } }),
  'pt-PT': () => import('../locales/pt-PT.json', { with: { type: 'json' } }),
  ro: () => import('../locales/ro.json', { with: { type: 'json' } }),
  ru: () => import('../locales/ru.json', { with: { type: 'json' } }),
  sk: () => import('../locales/sk.json', { with: { type: 'json' } }),
  sl: () => import('../locales/sl.json', { with: { type: 'json' } }),
  'sr-Cyrl': () => import('../locales/sr-Cyrl.json', { with: { type: 'json' } }),
  sv: () => import('../locales/sv.json', { with: { type: 'json' } }),
  tr: () => import('../locales/tr.json', { with: { type: 'json' } }),
  uk: () => import('../locales/uk.json', { with: { type: 'json' } }),
  vi: () => import('../locales/vi.json', { with: { type: 'json' } }),
  'zh-Hans': () => import('../locales/zh-Hans.json', { with: { type: 'json' } }),
  'zh-Hant': () => import('../locales/zh-Hant.json', { with: { type: 'json' } }),
};
async function load(locale: Locale): Promise<void> {
  if (catalogs.has(locale)) return;
  if (locale === 'en') {
    registerCatalog('en', english as Catalog);
    return;
  }
  let promise = pending.get(locale);
  if (!promise) {
    promise = (
      customLoader
        ? customLoader(locale)
        : (loaders[locale]?.() ?? Promise.reject(new Error('Unsupported language'))).then(
            (m) => m.default,
          )
    ).then((catalog) => {
      registerCatalog(locale, catalog);
    });
    pending.set(locale, promise);
    promise.catch(() => {
      pending.delete(locale);
    });
  }
  await promise;
}
function applyDocument(): void {
  if (scope.document) {
    scope.document.documentElement.lang = selected;
    scope.document.documentElement.dir = locales.find((l) => l.code === selected)?.dir ?? 'ltr';
  }
}
function commitLocale(locale: Locale, persist: boolean): void {
  selected = locale;
  applyDocument();
  try {
    if (persist && scope.document)
      scope.document.cookie = `glob2_locale=${encodeURIComponent(selected)}; Path=/; Max-Age=31536000; SameSite=Lax`;
  } catch {
    /* Memory preference still works when cookies are blocked. */
  }
  listeners.forEach((listener) => listener());
  if (scope.dispatchEvent && scope.CustomEvent)
    scope.dispatchEvent(new scope.CustomEvent('glob2:localechange'));
}
export async function setLocale(locale: string): Promise<void> {
  const match = locales.find((l) => l.code === locale);
  if (!match) throw new Error('Unsupported language');
  const request = ++revision;
  await load(match.code);
  if (request !== revision) return;
  commitLocale(match.code, true);
}
export function initialize(): Promise<void> {
  if (initialization) return initialization;
  initialization = (async () => {
    let cookie = '';
    try {
      cookie = scope.document?.cookie ?? '';
    } catch {
      /* Restricted storage. */
    }
    const saved = localeFromCookie(cookie);
    const initial =
      saved ?? resolveLocale(scope.navigator?.languages ?? [scope.navigator?.language ?? 'en']);
    const request = ++revision;
    try {
      await load(initial);
      if (request === revision) commitLocale(initial, saved !== undefined);
    } catch {
      // A transient download failure must not replace the saved preference, or
      // override a newer explicit choice made while initialization was pending.
      if (request !== revision) return;
      await load('en');
      if (request === revision) commitLocale('en', false);
    }
  })();
  return initialization;
}
export function t(source: string, params: Params = {}, locale: string = selected): string {
  const catalog = catalogs.get(locale);
  const message = catalog && Object.hasOwn(catalog, source) ? catalog[source] : undefined;
  let text: string;
  if (typeof message === 'string') text = message;
  else if (message) {
    const category = new Intl.PluralRules(locale).select(Number(params['count'] ?? 0));
    text =
      (Object.hasOwn(message, category) ? message[category] : undefined) ??
      (Object.hasOwn(message, 'other') ? message.other : undefined) ??
      source;
  } else text = source;
  return text.replace(/\{([a-zA-Z][a-zA-Z0-9_]*)\}/g, (placeholder, name: string) =>
    !Object.hasOwn(params, name) || params[name] === undefined
      ? placeholder
      : name === 'count' && typeof params[name] === 'number'
        ? new Intl.NumberFormat(locale).format(params[name])
        : String(params[name]),
  );
}
export function tp(singular: string, plural: string, count: number, params: Params = {}): string {
  const catalog = catalogs.get(selected);
  const message = catalog && Object.hasOwn(catalog, singular) ? catalog[singular] : undefined;
  if (message && typeof message !== 'string') return t(singular, { ...params, count });
  return t(count === 1 ? singular : plural, { ...params, count });
}
export function formatNumber(value: number, options?: Intl.NumberFormatOptions): string {
  return new Intl.NumberFormat(selected, options).format(value);
}
export function formatDate(
  value: Date | string | number,
  options?: Intl.DateTimeFormatOptions,
): string {
  return new Intl.DateTimeFormat(selected, options).format(
    typeof value === 'string' ? new Date(value) : value,
  );
}
export function formatRelative(
  value: number,
  unit: Intl.RelativeTimeFormatUnit,
  options?: Intl.RelativeTimeFormatOptions,
): string {
  return new Intl.RelativeTimeFormat(selected, { numeric: 'auto', ...options }).format(value, unit);
}
export function translateError(error: unknown): string {
  const item = error as
    | { body?: { messageKey?: string; messageParams?: Params; message?: string }; message?: string }
    | undefined;
  const source = item?.body?.messageKey ?? item?.body?.message ?? item?.message;
  if (source && (Object.hasOwn(english, source) || (item?.body?.messageKey && hasMessage(source))))
    return t(source, item?.body?.messageParams);
  return t('Something went wrong. Please try again.');
}
