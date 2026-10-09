import { registerCatalog } from '@glob2/i18n/server';
import type { FastifyReply, FastifyRequest } from 'fastify';
import { describe, expect, it } from 'vitest';
import { html, pageHtml, pageText, setPageLocale, sendPage } from '../src/web/pages.ts';

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
