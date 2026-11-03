import { describe, expect, it } from 'vitest';
import { aiBrowseUrl, aiReturnUrl, readAiBrowse } from '../src/aiBrowse.ts';

describe('AI catalogue navigation', () => {
  it('preserves catalogue view, filters, pagination, and the originating result', () => {
    const url =
      '/ais/favourites?q=patient&tags=Economy%2CDefensive&sort=downloads&pages=3&focus=ai-123';
    expect(aiReturnUrl(url)).toBe(url);
    expect(aiBrowseUrl('/ais/mine', readAiBrowse(new URLSearchParams('q=builder&pages=2')))).toBe(
      '/ais/mine?q=builder&pages=2',
    );
  });
  it('accepts only known local catalogue destinations', () => {
    for (const value of [
      null,
      '//evil.example/ais',
      'https://evil.example/ais',
      '/ais/new',
      '/ais/../admin',
      '/ais#other',
      '/ais?' + 'q=x'.repeat(1000),
    ])
      expect(aiReturnUrl(value)).toBe('/ais');
  });
  it('bounds untrusted query fields and discards unknown parameters', () => {
    const state = readAiBrowse(
      new URLSearchParams({
        q: 'a'.repeat(200),
        tags: 'Economy,unknown,Economy,Rush',
        sort: 'nope',
        pages: '999999',
        focus: '../bad',
      }),
    );
    expect(state).toEqual({
      query: 'a'.repeat(128),
      tags: ['Economy', 'Rush'],
      sort: 'likes',
      pages: 20,
      focus: '',
      cursor: '',
    });
    for (const pages of ['-1', '1.5', 'NaN', 'Infinity'])
      expect(readAiBrowse(new URLSearchParams({ pages })).pages).toBe(1);
    expect(aiReturnUrl('/ais?redirect=https://evil.example')).toBe('/ais');
    expect(aiReturnUrl('/ais?cursor=eyJpZCI6IjEyMyJ9&pages=2')).toBe(
      '/ais?pages=2&cursor=eyJpZCI6IjEyMyJ9',
    );
    expect(aiReturnUrl('/ais?cursor=' + 'a'.repeat(513))).toBe('/ais');
  });
});
