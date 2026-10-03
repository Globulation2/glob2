import type { FastifyReply } from 'fastify';
import { describe, expect, it } from 'vitest';
import { html, sendPage } from '../src/web/pages.ts';

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
