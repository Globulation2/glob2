// @vitest-environment jsdom
import { afterEach, expect, it } from 'vitest';
import { clearMusicStudioDraft } from '../src/pages/music-studio/useMusicStudioDraft.ts';

afterEach(() => sessionStorage.clear());

it('removes deleted conversation drafts and checkout recovery without touching other projects', () => {
  for (const suffix of ['owner:deleted', 'owner:other', 'owner:new', 'other:deleted'])
    for (const kind of ['draft', 'pending', 'settings', 'autosend'])
      sessionStorage.setItem(`music-studio-${kind}:${suffix}`, 'private state');
  sessionStorage.setItem('music-studio-checkout:owner', 'deleted');
  sessionStorage.setItem('music-studio-checkout-balance:owner', '4');
  sessionStorage.setItem('map-studio-draft:owner:deleted', 'map idea');

  clearMusicStudioDraft('owner', 'deleted');

  for (const kind of ['draft', 'pending', 'settings', 'autosend']) {
    expect(sessionStorage.getItem(`music-studio-${kind}:owner:deleted`)).toBeNull();
    for (const suffix of ['owner:other', 'owner:new', 'other:deleted'])
      expect(sessionStorage.getItem(`music-studio-${kind}:${suffix}`)).toBe('private state');
  }
  expect(sessionStorage.getItem('music-studio-checkout:owner')).toBeNull();
  expect(sessionStorage.getItem('music-studio-checkout-balance:owner')).toBeNull();
  expect(sessionStorage.getItem('map-studio-draft:owner:deleted')).toBe('map idea');
});

it('preserves checkout recovery for another conversation', () => {
  sessionStorage.setItem('music-studio-checkout:owner', 'other');
  sessionStorage.setItem('music-studio-checkout-balance:owner', '4');
  clearMusicStudioDraft('owner', 'deleted');
  expect(sessionStorage.getItem('music-studio-checkout:owner')).toBe('other');
  expect(sessionStorage.getItem('music-studio-checkout-balance:owner')).toBe('4');
});
