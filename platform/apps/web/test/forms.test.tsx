// @vitest-environment jsdom
// The map upload form (check first, create only on success, plain-language
// errors on the file field), invite codes pasted as links, headings on every
// page, keyboard-scrollable tables and the links between app and website.
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { App } from '../src/App.tsx';
import { inviteCodeFrom } from '../src/pages/Home.tsx';
import { playMapUrl, titleFromFileName } from '../src/pages/Maps.tsx';

const SIM = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };
const NOW = '2026-10-01T12:00:00Z';
const MAP_ID = '5f6a7b8c-9d0e-4f1a-8b2c-3d4e5f6a7b8c';
const UPLOAD_ID = '6a7b8c9d-0e1f-4a2b-9c3d-4e5f6a7b8c9d';

const instance = {
  name: 'Test Instance',
  origin: 'http://localhost',
  realtimeUrl: 'ws://localhost/realtime',
  supportedSimVersions: [SIM],
  authProviders: [],
  queues: [],
  guestsAllowed: true,
};
const me = {
  id: '0b8f6f2e-3c4d-4e5f-8a9b-0c1d2e3f4a5b',
  displayName: 'Mapper',
  kind: 'registered',
  createdAt: NOW,
  role: 'user',
  status: 'active',
  identities: [],
  entitlements: [],
};

type Handler = (init: RequestInit | undefined, url: URL) => Response | Promise<Response>;
let signedIn = true;
let handlers: Record<string, Handler>;
let calls: string[];

const upload = (status: string, extra: Record<string, unknown> = {}) => ({
  id: UPLOAD_ID,
  format: 'map',
  sha256: 'd'.repeat(64),
  size: 1234,
  simVersion: SIM,
  status,
  downloadUrl: 'http://localhost/api/v1/blobs/maps/x',
  createdAt: NOW,
  ...extra,
});

beforeAll(async () => {
  await Promise.all([import('@glob2/protocol'), import('../src/pages/Maps.tsx')]);
});

beforeEach(() => {
  signedIn = true;
  calls = [];
  handlers = {};
  vi.spyOn(window, 'scrollTo').mockImplementation(() => undefined);
  vi.stubGlobal(
    'fetch',
    vi.fn(async (input: string, init?: RequestInit) => {
      const url = new URL(input, 'http://localhost');
      const key = `${init?.method ?? 'GET'} ${url.pathname}`;
      calls.push(key);
      if (key === 'GET /api/v1/instance') return Response.json(instance);
      if (key === 'GET /api/v1/accounts/me') {
        return signedIn
          ? Response.json(me)
          : Response.json({ code: 'unauthenticated', message: 'Sign in.' }, { status: 401 });
      }
      const handler = handlers[key];
      if (handler) return handler(init, url);
      return Response.json({ code: 'not_found', message: 'No such thing.' }, { status: 404 });
    }),
  );
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

function open(path: string) {
  window.history.replaceState(null, '', path);
  return render(<App />);
}

async function chooseFileAndUpload(name = 'SmallForTwo.map.gz') {
  const input = await screen.findByLabelText(/Map file/);
  const file = new File([new Uint8Array([0x1f, 0x8b, 1, 2])], name);
  fireEvent.change(input, { target: { files: [file] } });
  fireEvent.click(screen.getByRole('button', { name: 'Upload' }));
  return input;
}

describe('map play choices', () => {
  const hash = 'd'.repeat(64);
  it('keeps the exact catalog version and selected destination in the link', () => {
    for (const mode of ['local', 'multiplayer'] as const) {
      const url = new URL(playMapUrl(MAP_ID, hash, 'Two & Three', mode), 'https://example.org');
      expect(url.pathname).toBe('/play/');
      expect(Object.fromEntries(url.searchParams)).toEqual({
        map: MAP_ID,
        version: hash,
        title: 'Two & Three',
        mode,
      });
    }
  });
  it('opens a modal with browser and installed-app links for both choices', async () => {
    Object.defineProperties(HTMLDialogElement.prototype, {
      showModal: {
        configurable: true,
        value() {
          this.open = true;
        },
      },
      close: {
        configurable: true,
        value() {
          this.open = false;
        },
      },
    });
    const show = vi.spyOn(HTMLDialogElement.prototype, 'showModal').mockImplementation(function (
      this: HTMLDialogElement,
    ) {
      this.open = true;
    });
    vi.spyOn(HTMLDialogElement.prototype, 'close').mockImplementation(function (
      this: HTMLDialogElement,
    ) {
      this.open = false;
    });
    const version = {
      hash,
      size: 1,
      width: 128,
      height: 128,
      teamCount: 2,
      validation: 'valid',
      preview: 'ready',
      downloadUrl: 'https://example.org/map.gz',
      createdAt: NOW,
    };
    handlers[`GET /api/v1/maps/${MAP_ID}`] = () =>
      Response.json({
        map: {
          id: MAP_ID,
          owner: me,
          title: 'Two & Three',
          description: '',
          visibility: 'public',
          hidden: false,
          madeWith: 'hand',
          latestVersion: version,
          stats: { plays: 0, downloads: 0, likes: 0 },
          createdAt: NOW,
          updatedAt: NOW,
        },
        versions: [version],
        viewer: { owner: false, moderator: false, liked: false, reported: false },
      });
    open(`/maps/${MAP_ID}`);
    const button = await screen.findByRole('button', { name: 'Play this map' });
    fireEvent.click(button);
    const modal = screen.getByRole('dialog', { name: 'How would you like to play?' });
    expect(show).toHaveBeenCalled();
    for (const [label, mode] of [
      ['Play Locally in Custom Game', 'local'],
      ['Play in Multiplayer', 'multiplayer'],
    ] as const) {
      const link = within(modal).getByRole('link', { name: new RegExp(label) });
      expect(new URL(link.getAttribute('href')!, 'http://localhost').searchParams.get('mode')).toBe(
        mode,
      );
    }
    const native = within(modal).getAllByRole('link', { name: 'Open in installed app' });
    expect(native).toHaveLength(2);
    for (const link of native) {
      const url = new URL(link.getAttribute('href')!);
      expect(url.protocol).toBe('glob2:');
      expect(url.searchParams.get('version')).toBe(hash);
      expect(url.searchParams.get('instance')).toBe(window.location.origin);
    }
    fireEvent.click(within(modal).getByRole('button', { name: 'Close play choices' }));
    expect(screen.queryByRole('dialog')).toBeNull();
  });
});

describe('map upload', () => {
  it('fills the title without the file extension', async () => {
    expect(titleFromFileName('SmallForTwo.map.gz')).toBe('SmallForTwo');
    expect(titleFromFileName('Two_Rivers.map')).toBe('Two Rivers');
    open('/maps/new');
    const input = await screen.findByLabelText(/Map file/);
    expect(input.getAttribute('accept')).toContain('.map.gz');
    fireEvent.change(input, {
      target: { files: [new File(['x'], 'SmallForTwo.map.gz')] },
    });
    expect((screen.getByLabelText('Title') as HTMLInputElement).value).toBe('SmallForTwo');
  });

  it('reports server upload validation on the file field and creates no map', async () => {
    handlers['POST /api/v1/uploads'] = () =>
      Response.json(
        {
          code: 'bad_request',
          message: 'format must be map or save.',
          messageKey: 'format must be map or save.',
          details: { problem: 'invalid_format' },
        },
        { status: 400 },
      );
    open('/maps/new');
    const input = await chooseFileAndUpload('holiday.jpg');
    const error = await screen.findByText('format must be map or save.');
    expect(error.id).toBe('map-file-error');
    expect(input.getAttribute('aria-invalid')).toBe('true');
    expect(input.getAttribute('aria-describedby')).toBe('map-file-error');
    expect(calls).not.toContain('POST /api/v1/maps');
  });

  it('waits for the game’s verdict and keeps an invalid file out of the catalog', async () => {
    handlers['POST /api/v1/uploads'] = () => Response.json(upload('pending'), { status: 201 });
    handlers[`GET /api/v1/uploads/${UPLOAD_ID}`] = () =>
      Response.json(
        upload('invalid', {
          reason: 'This map was made with a newer version of Globulation 2 than this server runs.',
        }),
      );
    open('/maps/new');
    await chooseFileAndUpload();
    expect(
      await screen.findByText(/newer version of Globulation 2/, {}, { timeout: 3000 }),
    ).toBeTruthy();
    expect(calls).not.toContain('POST /api/v1/maps');
  });

  it('creates the map only after the file loads, then opens its page', async () => {
    handlers['POST /api/v1/uploads'] = () =>
      Response.json(upload('valid', { map: { width: 64, height: 64, teamCount: 2 } }), {
        status: 201,
      });
    handlers['POST /api/v1/maps'] = () =>
      Response.json({ id: MAP_ID, title: 'SmallForTwo' }, { status: 201 });
    handlers[`POST /api/v1/maps/${MAP_ID}/versions`] = () =>
      Response.json({ hash: 'd'.repeat(64), validation: 'valid' }, { status: 201 });
    open('/maps/new');
    await chooseFileAndUpload();
    await waitFor(() => expect(window.location.pathname).toBe(`/maps/${MAP_ID}`));
    expect(calls.indexOf('POST /api/v1/uploads')).toBeLessThan(calls.indexOf('POST /api/v1/maps'));
  });

  it('keeps a heading when signed out', async () => {
    signedIn = false;
    open('/maps/new');
    expect(await screen.findByRole('heading', { level: 1, name: 'Upload a map' })).toBeTruthy();
    expect(screen.getAllByRole('link', { name: 'Sign in' }).length).toBeGreaterThan(0);
  });
});

describe('headings, tables and invites', () => {
  it('gives the not-found page a level-one heading', async () => {
    open('/no/such/page');
    expect(await screen.findByRole('heading', { level: 1, name: 'Page not found' })).toBeTruthy();
  });

  it('accepts a pasted invite link as well as a code', () => {
    expect(inviteCodeFrom('nvykahzud9')).toBe('NVYKAHZUD9');
    expect(inviteCodeFrom(' https://app.glob2online.com/j/NVYKAHZUD9 ')).toBe('NVYKAHZUD9');
    expect(inviteCodeFrom('https://app.glob2online.com/j/NVYKAHZUD9/?x=1')).toBe('NVYKAHZUD9');
    expect(inviteCodeFrom('glob2online')).toBe('GLOB2ONLINE');
    expect(inviteCodeFrom('https://example.org/')).toBeUndefined();
    expect(inviteCodeFrom('no')).toBeUndefined();
  });
});

describe('links between the app and the website', () => {
  afterEach(() => {
    vi.unstubAllEnvs();
    vi.resetModules();
  });

  it('downloads from the project when no website is configured', async () => {
    vi.resetModules();
    const home = await import('../src/pages/Home.tsx');
    expect(home.WEBSITE_URL).toBeUndefined();
    expect(home.DOWNLOAD_URL).toBe('https://github.com/Globulation2/glob2/releases');
    const { App: SiteApp } = await import('../src/App.tsx');
    window.history.replaceState(null, '', '/leaderboard');
    const { container } = render(<SiteApp />);
    await screen.findByRole('navigation', { name: 'Main' });
    expect(container.querySelector('.brand')?.getAttribute('href')).toBe('/');
  });

  it('keeps the brand in the app and exposes the website and download in About', async () => {
    vi.resetModules();
    vi.stubEnv('VITE_WEBSITE_URL', 'https://glob2online.com/');
    const home = await import('../src/pages/Home.tsx');
    expect(home.WEBSITE_URL).toBe('https://glob2online.com');
    expect(home.DOWNLOAD_URL).toBe('https://glob2online.com/downloads/');
    const { App: SiteApp } = await import('../src/App.tsx');
    window.history.replaceState(null, '', '/leaderboard');
    const { container } = render(<SiteApp />);
    const main = await screen.findByRole('navigation', { name: 'Main' });
    const sidebar = container.querySelector<HTMLElement>('.app-sidebar');
    const logo = sidebar!.querySelector('.brand');
    expect(logo?.getAttribute('href')).toBe('/');
    fireEvent.click(within(sidebar!).getByText('About & help'));
    const about = within(sidebar!).getByRole('navigation', { name: 'About' });
    expect(
      within(about)
        .getByRole('link', { name: 'Globulation 2 Online website' })
        .getAttribute('href'),
    ).toBe('https://glob2online.com');
    expect(
      within(about).getByRole('link', { name: 'Download the game' }).getAttribute('href'),
    ).toBe('https://glob2online.com/downloads/');
    expect(within(main).getByRole('link', { name: 'Home' }).getAttribute('href')).toBe('/');
  });
});
