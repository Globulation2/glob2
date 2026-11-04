// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { App } from '../src/App.tsx';
import { id, report, generator, version } from './generatorFixture.ts';
let published = 0;
beforeEach(() => {
  published = 0;
  window.matchMedia = vi
    .fn()
    .mockReturnValue({ matches: false, addEventListener: vi.fn(), removeEventListener: vi.fn() });
  window.scrollTo = vi.fn();
  vi.stubGlobal(
    'fetch',
    vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
      const u = new URL(String(input), 'http://localhost');
      if (u.pathname === '/api/v1/instance')
        return Response.json({
          name: 'Test',
          origin: 'http://localhost',
          realtimeUrl: 'ws://localhost/realtime',
          supportedSimVersions: [],
          authProviders: [],
          queues: [],
          guestsAllowed: true,
        });
      if (u.pathname === '/api/v1/accounts/me')
        return Response.json({
          id,
          kind: 'registered',
          displayName: 'Maple',
          createdAt: version.createdAt,
          role: 'user',
          status: 'active',
          identities: [],
          entitlements: [],
        });
      if (u.pathname === '/api/v1/generator-uploads')
        return Response.json({
          id,
          sourceHash: report.sourceHash,
          status: 'valid',
          report,
          expiresAt: '2099-01-01T00:00:00Z',
        });
      if (u.pathname === '/api/v1/generators' && init?.method === 'POST') {
        published++;
        return Response.json(generator);
      }
      if (u.pathname === '/api/v1/generators') return Response.json({ items: [] });
      if (u.pathname === '/api/v1/generators/' + id)
        return Response.json({
          generator,
          versions: [version],
          viewer: { owner: false, moderator: false },
        });
      return Response.json({ code: 'not_found', message: 'Missing' }, { status: 404 });
    }),
  );
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});
it('gates publication on bound validation and invalidates it after changing example settings', async () => {
  window.history.replaceState(null, '', '/generators/new');
  render(<App />);
  const publish = await screen.findByRole('button', { name: 'Publish release' });
  expect((publish as HTMLButtonElement).disabled).toBe(true);
  expect((screen.getByLabelText('Visibility') as HTMLSelectElement).value).toBe('unlisted');
  fireEvent.change(screen.getByLabelText('Package'), {
    target: { files: [new File(['{}'], 'generator.json')] },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Validate package' }));
  await screen.findByRole('heading', { name: 'Technical checks passed' });
  expect((publish as HTMLButtonElement).disabled).toBe(false);
  fireEvent.change(screen.getByLabelText('Seed'), { target: { value: '91' } });
  expect((publish as HTMLButtonElement).disabled).toBe(true);
  expect(published).toBe(0);
});
it('exposes exact releases and sampled refusals without claiming balance certification', async () => {
  window.history.replaceState(null, '', '/generators/' + id);
  render(<App />);
  await screen.findByRole('heading', { name: 'River Country' });
  expect(screen.getByRole('link', { name: 'Download exact package' }).getAttribute('href')).toBe(
    version.downloadUrl,
  );
  expect(screen.getByText(/no suitable colony sites/)).toBeTruthy();
  expect(screen.getByText(/do not certify balance/)).toBeTruthy();
});
it('searches tags and separates playable generators from editor tools', async () => {
  window.history.replaceState(null, '', '/generators');
  render(<App />);
  await screen.findByRole('heading', { name: 'Map generators' });
  fireEvent.change(screen.getByLabelText('Tags'), { target: { value: 'feature:rivers' } });
  fireEvent.change(screen.getByLabelText('Type'), { target: { value: 'true' } });
  await waitFor(() =>
    expect(
      vi
        .mocked(fetch)
        .mock.calls.some(
          ([u]) =>
            String(u).includes('tags=feature%3Arivers') && String(u).includes('editorOnly=true'),
        ),
    ).toBe(true),
  );
  expect(screen.getAllByRole('link', { name: 'Maps' }).length).toBeGreaterThan(0);
});
