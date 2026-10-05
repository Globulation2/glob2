// @vitest-environment jsdom
import { afterEach, beforeEach, it, expect, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { pendingAiReport } from '@glob2/protocol';
import { App } from '../src/App.tsx';
const id = '11111111-1111-4111-8111-111111111111',
  now = '2026-10-04T12:00:00Z';
const report = pendingAiReport('a'.repeat(64), '129-51-' + 'b'.repeat(64));
report.valid = true;
report.checks.forEach((c) => (c.status = 'passed'));
report.metadata = {
  apiVersion: 2,
  name: 'Patient Gardener',
  description: 'A careful economic strategy.',
  version: '1.0',
  author: 'Author',
};
let published = 0,
  uploadNumber = 0;
beforeEach(() => {
  published = 0;
  uploadNumber = 0;
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
          displayName: 'Author',
          createdAt: now,
          role: 'user',
          status: 'active',
          identities: [],
          entitlements: [],
        });
      if (u.pathname === '/api/v1/ai-uploads') {
        uploadNumber++;
        return Response.json({
          id,
          sourceHash: report.sourceHash,
          report:
            uploadNumber === 1
              ? report
              : {
                  ...report,
                  valid: false,
                  checks: report.checks.map((c) => ({
                    ...c,
                    status: c.id === 'syntax' ? 'failed' : 'skipped',
                  })),
                },
          status: uploadNumber === 1 ? 'valid' : 'invalid',
          expiresAt: now,
        });
      }
      if (u.pathname === '/api/v1/ais' && init?.method === 'POST') {
        published++;
        return Response.json({ id });
      }
      if (u.pathname === '/api/v1/ais') return Response.json({ items: [] });
      return Response.json({ code: 'not_found', message: 'Missing' }, { status: 404 });
    }),
  );
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});
it('requires a passing checklist and invalidates it when the file changes', async () => {
  window.history.replaceState(null, '', '/ais/new');
  render(<App />);
  const publish = await screen.findByRole('button', { name: 'Publish' });
  expect((publish as HTMLButtonElement).disabled).toBe(true);
  const file = await screen.findByLabelText(/Bundled JavaScript file/);
  fireEvent.change(file, {
    target: { files: [new File(['function step(){}'], 'ai.js', { type: 'text/javascript' })] },
  });
  await screen.findByText('Ready to publish');
  expect((publish as HTMLButtonElement).disabled).toBe(false);
  expect((screen.getByLabelText('Name') as HTMLInputElement).value).toBe('Patient Gardener');
  expect(screen.getByRole('list', { name: 'Compatibility checks' }).children).toHaveLength(7);
  fireEvent.change(file, { target: { files: [new File(['bad !'], 'broken.js')] } });
  await screen.findByText('Fix the failed checks and choose your updated file.');
  expect((publish as HTMLButtonElement).disabled).toBe(true);
  expect(published).toBe(0);
});
it('searches and filters the shared catalogue with a useful empty state', async () => {
  window.history.replaceState(null, '', '/ais');
  render(<App />);
  await screen.findByRole('heading', { name: 'AI Library' });
  fireEvent.change(screen.getByRole('searchbox', { name: 'Search AIs' }), {
    target: { value: 'builder' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Economy' }));
  await waitFor(() =>
    expect(
      vi
        .mocked(fetch)
        .mock.calls.some(
          ([u]) => String(u).includes('q=builder') && String(u).includes('tags=Economy'),
        ),
    ).toBe(true),
  );
  await screen.findByText('No AIs match these filters.');
  expect(screen.getByRole('link', { name: 'Share your AI' }).getAttribute('href')).toBe('/ais/new');
});

it('shows a failed file check and skipped dependent checks for an empty file', async () => {
  window.history.replaceState(null, '', '/ais/new');
  render(<App />);
  fireEvent.change(await screen.findByLabelText(/Bundled JavaScript file/), {
    target: { files: [new File([], 'empty.js')] },
  });
  await waitFor(() =>
    expect(document.querySelectorAll('.ai-checklist [data-status="failed"]')).toHaveLength(1),
  );
  expect(document.querySelectorAll('.ai-checklist [data-status="skipped"]')).toHaveLength(6);
  expect((screen.getByRole('button', { name: 'Publish' }) as HTMLButtonElement).disabled).toBe(
    true,
  );
  expect(uploadNumber).toBe(0);
});
