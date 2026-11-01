import { registerCatalog } from '@glob2/i18n/server';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import Fastify from 'fastify';
import type { FastifyReply, FastifyRequest } from 'fastify';
import { describe, expect, it } from 'vitest';
import {
  html,
  isolateNumericRanges,
  pageHtml,
  pageText,
  setPageLocale,
  sendPage,
  pageAssetRoutes,
} from '../src/web/pages.ts';

it('serves shared bootstrap, rebased CSS, fonts and notices under the form CSP', async () => {
  const app = Fastify();
  await app.register(pageAssetRoutes);
  app.get('/form', (_request, reply) => sendPage(reply, 'Sign in', html`<form></form>`));
  try {
    const page = await app.inject('/form');
    expect(page.body).toContain('<script src="/signin/assets/bootstrap.js"></script>');
    expect(page.body).toContain('class="g2-ui"');
    expect(page.body).toContain('href="/signin/assets/script-fonts.css"');
    expect(page.headers['content-security-policy']).toContain("script-src 'self'");
    expect(page.headers['content-security-policy']).not.toContain("script-src 'unsafe-inline'");
    for (const [file, type] of [
      ['bootstrap.js', 'text/javascript'],
      ['tokens.css', 'text/css'],
      ['glob2-sans.woff2', 'font/woff2'],
      ['LICENSE-Nunito.txt', 'text/plain'],
    ]) {
      const response = await app.inject(`/signin/assets/${file}`);
      expect(response.statusCode, file).toBe(200);
      expect(response.headers['content-type']).toContain(type);
    }
    const fonts = await app.inject('/signin/assets/fonts.css');
    expect(fonts.body).toContain('/signin/assets/glob2-sans.woff2');
    expect(fonts.body).not.toContain('../../assets/');
    const scriptCss = await app.inject('/signin/assets/script-fonts.css');
    expect(scriptCss.statusCode).toBe(200);
    expect(scriptCss.body).not.toContain('../../../data/');
    expect(scriptCss.body).not.toContain('../../../browser/');
    expect(scriptCss.body).toContain('/signin/assets/sans-core.ttf');
    expect(scriptCss.body).toContain(':root:lang(bg)');
    expect(scriptCss.body).toContain('option:lang(bg)');
    const coreFont = await app.inject('/signin/assets/sans-core.ttf');
    expect(coreFont.statusCode).toBe(200);
    expect(coreFont.headers['content-type']).toContain('font/ttf');
    expect(coreFont.rawPayload).toEqual(
      readFileSync(new URL('../../../../browser/assets/sans-core.ttf', import.meta.url)),
    );
    const dejavuLicence = await app.inject('/signin/assets/LICENSE-DejaVu.txt');
    expect(dejavuLicence.statusCode).toBe(200);
    expect(dejavuLicence.headers['content-type']).toContain('text/plain');
    expect(dejavuLicence.rawPayload).toEqual(
      readFileSync(new URL('../../../../data/fonts/LICENSE-DejaVu.txt', import.meta.url)),
    );
    const manifest = JSON.parse(
      readFileSync(
        new URL('../../../../data/fonts/scripts/manifest.json', import.meta.url),
        'utf8',
      ),
    ) as { fonts: { name: string; sha256: string; bytes: number }[] };
    for (const font of manifest.fonts) {
      const file = `${font.name}-Regular.ttf`;
      expect(scriptCss.body).toContain(`/signin/assets/${file}`);
      const response = await app.inject(`/signin/assets/${file}`);
      expect(response.statusCode, file).toBe(200);
      expect(response.headers['content-type']).toContain('font/ttf');
      expect(response.rawPayload.length).toBe(font.bytes);
      expect(createHash('sha256').update(response.rawPayload).digest('hex')).toBe(font.sha256);
    }
    const droidLicence = await app.inject('/signin/assets/LICENSE-Droid.txt');
    expect(droidLicence.statusCode).toBe(200);
    expect(droidLicence.rawPayload).toEqual(
      readFileSync(new URL('../../../../data/fonts/LICENSE-Droid.txt', import.meta.url)),
    );
    const licence = await app.inject('/signin/assets/LICENSE-Noto.txt');
    expect(licence.statusCode).toBe(200);
    expect(licence.body).toContain('SIL OPEN FONT LICENSE');
    expect((await app.inject('/signin/assets/unapproved.ttf')).statusCode).toBe(404);
    expect((await app.inject('/signin/assets/%2e%2e%2fpackage.json')).statusCode).toBe(404);
  } finally {
    await app.close();
  }
});

describe('browser form security headers', () => {
  it('preserves same-origin form Origin without leaking referrers to external sites', () => {
    const headers = new Map<string, string>();
    const reply = {
      status() {
        return this;
      },
      header(name: string, value: string) {
        headers.set(name, value);
        return this;
      },
      send() {
        return this;
      },
    } as unknown as FastifyReply;
    sendPage(reply, 'Sign in', html`<form method="post" action="/signin/local"></form>`);
    expect(headers.get('referrer-policy')).toBe('same-origin');
    expect(headers.get('content-security-policy')).toContain("form-action 'self'");
    expect(headers.get('content-security-policy')).toContain("frame-ancestors 'none'");
    expect(headers.get('cache-control')).toBe('no-store');
  });
});

describe('localized server pages', () => {
  function response() {
    let body = '';
    const reply = {
      status() {
        return this;
      },
      header() {
        return this;
      },
      send(value: string) {
        body = value;
        return this;
      },
    } as unknown as FastifyReply;
    return { reply, body: () => body };
  }
  it('keeps cookie and header language choices scoped to each response', () => {
    const arabic = response();
    const french = response();
    setPageLocale(
      { headers: { cookie: 'glob2_locale=ar', 'accept-language': 'fr' } } as FastifyRequest,
      arabic.reply,
    );
    setPageLocale(
      { headers: { 'accept-language': 'en;q=0,fr;q=0.9,de;q=0.5' } } as FastifyRequest,
      french.reply,
    );
    sendPage(arabic.reply, 'Sign in', pageHtml(arabic.reply)`<p>Sign in</p>`);
    sendPage(french.reply, 'Sign in', pageHtml(french.reply)`<p>Sign in</p>`);
    expect(arabic.body()).toContain('<html lang="ar" dir="rtl">');
    expect(french.body()).toContain('<html lang="fr" dir="ltr">');
  });
  it.each([
    ['Globulation 2', ''],
    ['Globulation 2 Online', ''],
    ['Glob2 Online (test)', ' Glob2 Online (test)-এ'],
    ['বাংলা বন্ধুদের সার্ভার', ' বাংলা বন্ধুদের সার্ভার-এ'],
  ])('composes Bengali invitations on %s without repeating the locative', (instance, suffix) => {
    const { reply } = response();
    setPageLocale({ headers: { cookie: 'glob2_locale=bn' } } as FastifyRequest, reply);
    const onInstance = /^Globulation 2\b/.test(instance)
      ? ''
      : pageText(reply, ' on {p0}', { p0: instance });
    expect(onInstance).toBe(suffix);
    expect(
      pageText(reply, '{p0} invited you to their Globulation 2 room{p1}.', {
        p0: 'Mirelle',
        p1: onInstance,
      }),
    ).toBe(`Mirelle আপনাকে${suffix} তাঁদের Globulation 2 রুমে আমন্ত্রণ জানিয়েছেন।`);
  });
  it('renders the shipped Marathi catalog on server pages using the saved preference', () => {
    const marathi = response();
    setPageLocale(
      { headers: { cookie: 'glob2_locale=mr', 'accept-language': 'en' } } as FastifyRequest,
      marathi.reply,
    );
    sendPage(marathi.reply, 'Sign in', pageHtml(marathi.reply)`<p>Sign in</p>`);
    const catalog = JSON.parse(
      readFileSync(new URL('../../../packages/i18n/locales/mr.json', import.meta.url), 'utf8'),
    ) as Record<string, string>;
    expect(catalog['Sign in']).toMatch(/[\u0900-\u097f]/u);
    expect(marathi.body()).toContain('<html lang="mr" dir="ltr">');
    expect(marathi.body()).toContain(`<p>${catalog['Sign in']}</p>`);
    expect(marathi.body()).toContain('/signin/assets/script-fonts.css');
  });
  it('escapes values, preserves rich slots and entities, and leaves scripts alone', () => {
    const { reply } = response();
    const attacker = '<script>alert(1)</script>';
    expect(pageHtml(reply)`<p>Continue as ${attacker}</p>`.value).toBe(
      '<p>Continue as &#60;script&#62;alert(1)&#60;/script&#62;</p>',
    );
    expect(pageHtml(reply)`<p>Open ${html`<strong>${attacker}</strong>`} now.</p>`.value).toContain(
      'Open <strong>&#60;script&#62;',
    );
    expect(pageHtml(reply)`<p>Maps &amp; games</p>`.value).toBe('<p>Maps &amp; games</p>');
    expect(pageHtml(reply)`<script>const text = 'Sign in';</script>`.value).toBe(
      "<script>const text = 'Sign in';</script>",
    );
    expect(pageText(reply, 'At least {p0} characters.', { p0: 10 })).toBe(
      'At least 10 characters.',
    );
  });
});

it('reorders rich translation slots without exposing markup from text values', () => {
  registerCatalog('fr', { 'Greeting {p0} and {p1}': 'Bonjour {p1}, {p0}.' });
  const reply = {} as FastifyReply;
  setPageLocale({ headers: { cookie: 'glob2_locale=fr' } } as FastifyRequest, reply);
  const label = html`<a href="/signin">link</a>`;
  expect(pageHtml(reply)`<p>Greeting ${'<script>x</script>'} and ${label}</p>`.value).toBe(
    '<p>Bonjour <a href="/signin">link</a>, &#60;script&#62;x&#60;/script&#62;.</p>',
  );
});

it('isolates numeric ranges without trusting surrounding text as markup', () => {
  expect(isolateNumericRanges('اردو (0–9) <script> & ۳ / ۹').value).toBe(
    'اردو (<bdi dir="ltr">0–9</bdi>) &#60;script&#62; &#38; <bdi dir="ltr">۳ / ۹</bdi>',
  );
});
