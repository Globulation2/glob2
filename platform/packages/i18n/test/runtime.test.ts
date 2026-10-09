import { beforeEach, afterEach, describe, expect, it, vi } from 'vitest';
import {
  locales,
  localeFromCookie,
  resolveLocale,
  registerCatalog,
  hasMessage,
  setLocale,
  getLocale,
  t,
  tp,
  formatNumber,
  subscribe,
  translateError,
} from '../src/index.ts';
const browserScope = globalThis as unknown as {
  document: { cookie: string; documentElement: { lang: string; dir: string } };
};
beforeEach(async () => {
  registerCatalog('en', {});
  await setLocale('en');
});
afterEach(() => {
  vi.unstubAllGlobals();
  vi.resetModules();
});
describe('online locale resolution', () => {
  it('maps standard browser tags without confusing game aliases', () => {
    expect(resolveLocale(['br', 'fr-CA'])).toBe('fr');
    expect(resolveLocale(['si', 'de'])).toBe('de');
    expect(resolveLocale('pt')).toBe('pt-PT');
    expect(resolveLocale('pt_BR')).toBe('pt-BR');
    expect(resolveLocale('zh-HK')).toBe('zh-Hant');
    expect(resolveLocale('zh')).toBe('zh-Hans');
    expect(resolveLocale('sr-RS')).toBe('sr-Cyrl');
    expect(resolveLocale('in-ID')).toBe('id');
    expect(resolveLocale('xx, fr;q=0.2, de;q=0.8')).toBe('de');
    expect(resolveLocale('fr;q=0, de')).toBe('de');
  });
  it('accepts only supported cookie values', () => {
    expect(localeFromCookie('other=1; glob2_locale=pt-BR; theme=dark')).toBe('pt-BR');
    expect(localeFromCookie('glob2_locale=%')).toBeUndefined();
    expect(localeFromCookie('glob2_locale=<script>')).toBeUndefined();
  });
  it('preserves language and sends no event on unsupported switches', async () => {
    await expect(setLocale('unsupported')).rejects.toThrow();
    expect(getLocale()).toBe('en');
  });
});
describe('message presentation', () => {
  it('interpolates safely as text and falls back to source', async () => {
    registerCatalog('fr', { 'Hello {name}': 'Bonjour {name}' });
    await setLocale('fr');
    expect(t('Hello {name}', { name: '<b>$&{name}</b>' })).toBe('Bonjour <b>$&{name}</b>');
    expect(t('Missing {value}', { value: 12 })).toBe('Missing 12');
  });
  it('treats inherited catalog and parameter names as missing', async () => {
    registerCatalog('fr', {});
    await setLocale('fr');
    for (const source of ['constructor', '__proto__', 'toString']) {
      expect(hasMessage(source)).toBe(false);
      expect(t(source)).toBe(source);
      expect(translateError({ body: { messageKey: source } })).toBe(
        'Something went wrong. Please try again.',
      );
    }
    expect(t('Hello {toString}')).toBe('Hello {toString}');
    expect(t('Hello {toString}', { toString: 'world' })).toBe('Hello world');
  });
  it('supports language-specific plural forms', async () => {
    registerCatalog('ar', {
      '{count} item': {
        zero: 'zero {count}',
        one: 'one {count}',
        two: 'two {count}',
        few: 'few {count}',
        many: 'many {count}',
        other: 'other {count}',
      },
    });
    await setLocale('ar');
    expect(tp('{count} item', '{count} items', 2)).toBe(
      `two ${new Intl.NumberFormat('ar').format(2)}`,
    );
    expect(tp('{count} item', '{count} items', 3)).toBe(
      `few ${new Intl.NumberFormat('ar').format(3)}`,
    );
    expect(tp('{count} item', '{count} items', 11)).toBe(
      `many ${new Intl.NumberFormat('ar').format(11)}`,
    );
  });
  it('notifies subscribers and formats with the selected locale', async () => {
    registerCatalog('de', {});
    let updates = 0;
    const stop = subscribe(() => updates++);
    await setLocale('de');
    expect(updates).toBe(1);
    stop();
    expect(formatNumber(1234.5)).toBe('1.234,5');
    expect(t('{count}', { count: 1234.5 })).toBe('1.234,5');
  });
  it('uses stable error messages and hides unknown diagnostics', async () => {
    registerCatalog('fr', {
      'Room {id} not found.': 'Salon {id} introuvable.',
      'Something went wrong. Please try again.': 'Veuillez réessayer.',
    });
    await setLocale('fr');
    expect(
      translateError({
        body: { messageKey: 'Room {id} not found.', messageParams: { id: 'abc' } },
      }),
    ).toBe('Salon abc introuvable.');
    expect(translateError(new Error('private diagnostic'))).toBe('Veuillez réessayer.');
  });
  it('contains 33 unique game catalog mappings', () => {
    expect(locales).toHaveLength(33);
    expect(new Set(locales.map((l) => l.gameCode)).size).toBe(33);
  });
});

describe('startup loading and presentation', () => {
  async function fresh(cookie = 'glob2_locale=fr') {
    vi.stubGlobal('document', { cookie, documentElement: { lang: 'en', dir: 'ltr' } });
    vi.resetModules();
    return import('../src/index.ts');
  }
  it('shares initialization and keeps a saved choice on transient failure', async () => {
    const runtime = await fresh();
    let calls = 0;
    runtime.configureCatalogLoader(async () => {
      calls++;
      throw new Error('download unavailable');
    });
    const first = runtime.initialize();
    expect(runtime.initialize()).toBe(first);
    await first;
    expect(calls).toBe(1);
    expect(runtime.getLocale()).toBe('en');
    expect(browserScope.document.cookie).toBe('glob2_locale=fr');
    expect(browserScope.document.documentElement.dir).toBe('ltr');
    runtime.configureCatalogLoader(async () => ({ Language: 'Langue' }));
    await runtime.setLocale('fr');
    expect(runtime.t('Language')).toBe('Langue');
  });
  it('does not persist automatic browser-language detection', async () => {
    const runtime = await fresh('other_cookie=1');
    vi.stubGlobal('navigator', { languages: ['fr'] });
    runtime.configureCatalogLoader(async () => ({}));
    await runtime.initialize();
    expect(runtime.getLocale()).toBe('fr');
    expect(browserScope.document.cookie).toBe('other_cookie=1');
    await runtime.setLocale('fr');
    expect(browserScope.document.cookie).toContain('glob2_locale=fr');
  });
  it('a failed initial download cannot override a newer successful switch', async () => {
    const runtime = await fresh();
    let fail!: (reason: Error) => void;
    runtime.configureCatalogLoader((locale) =>
      locale === 'fr'
        ? new Promise((_, reject) => {
            fail = reject;
          })
        : Promise.resolve({ Language: 'العربية' }),
    );
    const initial = runtime.initialize();
    await runtime.setLocale('ar');
    fail(new Error('initial download failed later'));
    await initial;
    expect(runtime.getLocale()).toBe('ar');
    expect(browserScope.document.cookie).toContain('glob2_locale=ar');
    expect(browserScope.document.documentElement.lang).toBe('ar');
    expect(browserScope.document.documentElement.dir).toBe('rtl');
  });
  it('switching from an RTL locale restores LTR document state', async () => {
    const runtime = await fresh('glob2_locale=ar');
    runtime.configureCatalogLoader(async () => ({}));
    await runtime.initialize();
    expect(browserScope.document.documentElement.dir).toBe('rtl');
    await runtime.setLocale('en');
    expect(browserScope.document.documentElement.lang).toBe('en');
    expect(browserScope.document.documentElement.dir).toBe('ltr');
  });
  it('English also hides unknown raw diagnostics while showing known public errors', async () => {
    const runtime = await fresh('glob2_locale=en');
    await runtime.initialize();
    expect(runtime.translateError(new Error('private diagnostic with secrets'))).toBe(
      'Something went wrong. Please try again.',
    );
    expect(runtime.translateError({ body: { message: 'private diagnostic' } })).toBe(
      'Something went wrong. Please try again.',
    );
    expect(runtime.translateError(new Error('Sign in first.'))).toBe('Sign in first.');
  });
});
