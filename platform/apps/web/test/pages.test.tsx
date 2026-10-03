// @vitest-environment jsdom
// Component tests of the web app's key pages with a stubbed API: routing of
// the deep links the game uses, leaderboard, profile, match page (charts and
// Watch in browser), sign-in state and the moderation guard.
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { App } from '../src/App.tsx';
import { LineChart } from '../src/components/LineChart.tsx';
import { matchPath } from '../src/router.tsx';
import { watchUrl } from '../src/pages/Match.tsx';

const SIM = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };
const ALICE = '0b8f6f2e-3c4d-4e5f-8a9b-0c1d2e3f4a5b';
const BOB = '1c9a7a3f-4d5e-4f60-9bac-1d2e3f4a5b6c';
const MATCH = '7e3c1d2b-9a8f-4e6d-8c5b-4a3f2e1d0c9b';
const NOW = '2026-10-01T12:00:00Z';

const instance = {
  name: 'Test Instance',
  origin: 'http://localhost',
  realtimeUrl: 'ws://localhost/realtime',
  supportedSimVersions: [SIM],
  authProviders: [],
  queues: [{ id: 'ranked-1v1', name: '1 vs 1 ranked', mode: '1v1', rated: true }],
  guestsAllowed: true,
};

const account = (id: string, displayName: string) => ({
  id,
  displayName,
  kind: 'registered',
  createdAt: NOW,
});

const summary = {
  id: MATCH,
  simVersion: SIM,
  origin: 'queue',
  queueId: 'ranked-1v1',
  rated: true,
  status: 'ended',
  verification: 'verified',
  mapHash: 'a'.repeat(64),
  mapTitle: 'Even Ground',
  startedAt: NOW,
  endedAt: NOW,
  durationTicks: 30_000,
  participants: [
    {
      seat: 0,
      team: 0,
      kind: 'human',
      displayName: 'Alice',
      accountId: ALICE,
      outcome: 'won',
      disconnects: 0,
      rating: { ladder: 'ranked-1v1', before: 1500, after: 1532, provisional: false },
    },
    {
      seat: 1,
      team: 1,
      kind: 'ai',
      displayName: 'AI',
      ai: 'nicowar',
      outcome: 'lost',
      disconnects: 0,
    },
  ],
};

const timeline = (pace: number) =>
  [0, 512, 1024].map((tick, i) => ({
    tick,
    units: 6 + i * pace,
    buildings: 1 + i,
    prestige: i * pace,
    hp: 0,
    attack: 0,
    defense: 0,
  }));

const routes: Record<string, unknown> = {
  '/api/v1/instance': instance,
  '/api/v1/leaderboards/ranked-1v1': {
    ladder: 'ranked-1v1',
    entries: [
      {
        rank: 1,
        entity: { kind: 'account', account: account(ALICE, 'Alice') },
        rating: 1712.4,
        mu: 29,
        sigma: 3,
        games: 40,
        wins: 30,
        provisional: false,
      },
      {
        rank: 2,
        entity: { kind: 'account', account: account(BOB, 'Bob') },
        rating: 1490,
        mu: 26,
        sigma: 6,
        games: 3,
        wins: 1,
        provisional: true,
      },
    ],
  },
  '/api/v1/leaderboards/ranked-1v1/ai': {
    ladder: 'ranked-1v1',
    groups: [
      {
        simVersion: SIM,
        current: true,
        entries: [
          {
            rank: 1,
            entity: { kind: 'ai', ai: 'nicowar', simVersion: SIM },
            rating: 1600,
            mu: 28,
            sigma: 3,
            games: 9,
            wins: 4,
            provisional: false,
          },
        ],
      },
    ],
  },
  [`/api/v1/players/${ALICE}`]: {
    account: account(ALICE, 'Alice'),
    detail: 'full',
    ratings: [
      {
        ladder: 'ranked-1v1',
        rating: 1712.4,
        mu: 29,
        sigma: 3,
        games: 40,
        wins: 30,
        provisional: false,
        rank: 1,
      },
    ],
    ratingHistory: [
      {
        ladder: 'ranked-1v1',
        matchId: MATCH,
        at: NOW,
        result: 'won',
        before: 1500,
        after: 1532,
        provisional: false,
      },
    ],
    recentMatches: [summary],
    aggregates: {
      windowDays: 90,
      games: 1,
      wins: 1,
      losses: 0,
      winRates: [
        {
          dimension: 'queue',
          key: 'ranked-1v1',
          label: '1 vs 1 ranked',
          games: 1,
          wins: 1,
          winRate: 1,
        },
      ],
      medianTicks: 30_000,
    },
  },
  [`/api/v1/players/${ALICE}/matches`]: { items: [summary] },
  [`/api/v1/matches/${MATCH}`]: {
    match: summary,
    setup: {},
    teams: [
      {
        team: 0,
        outcome: 'won',
        prestige: 50,
        statistics: { unitsProduced: 30 },
        timeline: timeline(5),
      },
      {
        team: 1,
        outcome: 'lost',
        prestige: 10,
        statistics: { unitsProduced: 12 },
        timeline: timeline(2),
      },
    ],
    artifacts: [
      {
        kind: 'replay',
        url: `http://localhost/api/v1/matches/${MATCH}/artifacts/replay`,
        size: 1000,
        sha256: 'c'.repeat(64),
      },
    ],
    map: { title: 'Even Ground', width: 128, height: 128 },
    verificationDetail: {
      orderRejections: [{ seat: 0, rejected: 2, stale: 0, reasons: { foreign_unit: 2 } }],
    },
    network: [
      {
        seat: 0,
        quality: 'fair',
        rttMs: { p50: 84, p95: 231 },
        lagMs: { p50: 280, p95: 440 },
        disconnects: 1,
        offlineMs: 4200,
        ordersSequenced: 412,
        ordersDeferred: 6,
        rejoins: 0,
      },
    ],
  },
  '/api/v1/matches': { items: [summary] },
};

let me: unknown;

// The app loads the schema library and the less visited pages on demand;
// load them once up front so each test sees the app as a warm browser would.
beforeAll(async () => {
  await Promise.all([
    import('@glob2/protocol'),
    import('../src/admin/Admin.tsx'),
    import('../src/pages/Maps.tsx'),
    import('../src/pages/Match.tsx'),
    import('../src/pages/Player.tsx'),
    import('../src/pages/Account.tsx'),
  ]);
});

beforeEach(() => {
  me = undefined;
  vi.spyOn(window, 'scrollTo').mockImplementation(() => undefined);
  vi.stubGlobal(
    'fetch',
    vi.fn(async (input: string) => {
      const path = new URL(input, 'http://localhost').pathname;
      if (path === '/api/v1/accounts/me') {
        return me
          ? Response.json(me)
          : Response.json({ code: 'unauthenticated', message: 'Sign in first.' }, { status: 401 });
      }
      const body = routes[path];
      return body
        ? Response.json(body)
        : Response.json({ code: 'not_found', message: 'No such thing.' }, { status: 404 });
    }),
  );
});

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

function open(path: string) {
  window.history.replaceState(null, '', path);
  return render(<App />);
}

describe('routing', () => {
  it('matches the deep links the game uses', () => {
    expect(matchPath('/players/:id', `/players/${ALICE}`)).toEqual({ id: ALICE });
    expect(matchPath('/leaderboard/:queueId', '/leaderboard/ranked-1v1')).toEqual({
      queueId: 'ranked-1v1',
    });
    expect(matchPath('/maps/:id', '/maps/a/b')).toBeUndefined();
    expect(matchPath('/maps/:id', '/maps/%E0%A4%A')).toBeUndefined();
  });

  it('builds Watch in browser links for the browser client', () => {
    expect(watchUrl('https://app.glob2online.com/api/v1/matches/x/artifacts/replay')).toBe(
      '/play/?replay=https%3A%2F%2Fapp.glob2online.com%2Fapi%2Fv1%2Fmatches%2Fx%2Fartifacts%2Freplay',
    );
  });

  it('shows a not-found page for unknown paths', async () => {
    open('/nowhere');
    expect(await screen.findByText(/Nothing here/)).toBeTruthy();
  });
});

describe('pages', () => {
  it('leaderboard lists players with provisional flags and AIs', async () => {
    open('/leaderboard/ranked-1v1');
    const rows = await screen.findAllByTestId('leaderboard-row');
    expect(rows).toHaveLength(3);
    expect(within(rows[0]!).getByText('Alice')).toBeTruthy();
    expect(within(rows[0]!).getByText('1712')).toBeTruthy();
    expect(within(rows[1]!).getByText('provisional')).toBeTruthy();
    expect(within(rows[2]!).getByText('Nicowar')).toBeTruthy();
  });

  it('player page shows ratings, rating graph and matches', async () => {
    open(`/players/${ALICE}`);
    expect((await screen.findByTestId('player-name')).textContent).toBe('Alice');
    expect(screen.getByTestId('rating-tile').textContent).toContain('#1');
    expect(screen.getByRole('img', { name: 'Rating after each rated match' })).toBeTruthy();
    const row = screen.getAllByTestId('match-row')[0]!;
    expect(row.getAttribute('href')).toBe(`/matches/${MATCH}`);
    expect(row.textContent).toContain('vs Nicowar (AI)');
    expect(row.textContent).toContain('+32');
  });

  it('match page shows players, timelines, refused orders and Watch in browser', async () => {
    open(`/matches/${MATCH}`);
    expect((await screen.findByTestId('match-title')).textContent).toBe(
      '1 vs 1 ranked · Even Ground',
    );
    expect(screen.getAllByTestId('participant')).toHaveLength(2);
    expect(within(screen.getByTestId('timelines')).getAllByRole('img')).toHaveLength(3);
    expect(screen.getByText('refused orders')).toBeTruthy();
    // Connection quality per human player, from the relay's report.
    const network = screen.getAllByTestId('network-row');
    expect(network).toHaveLength(1);
    expect(network[0]!.textContent).toContain('Alice');
    expect(within(network[0]!).getByText('Fair').className).toBe('badge warn');
    // Every value has its unit and its word from the shared table.
    expect(network[0]!.textContent).toContain('84 ms · Good');
    expect(network[0]!.textContent).toContain('95%: 231 ms');
    expect(network[0]!.textContent).toContain('0.3 s · Good');
    expect(network[0]!.textContent).toContain('95%: 0.4 s');
    expect(network[0]!.textContent).toContain('4.2 s');
    expect(screen.getByTestId('network-legend').textContent).toContain(
      'Good under 150 ms, fair under 300 ms, poor from 300 ms.',
    );
    expect(screen.getByTestId('network-legend').textContent).toContain(
      'Good under 1 s, fair under 2 s, poor from 2 s.',
    );
    expect(network[0]!.textContent).toContain('6 / 412');
    // Tables that may scroll sideways are named, focusable regions; on phones the
    // connection table stacks its rows into labelled cards instead.
    const connection = screen.getByRole('region', { name: 'Connection quality per player' });
    expect(connection.tabIndex).toBe(0);
    expect(connection.className).toContain('stack');
    expect(
      within(network[0]!)
        .getAllByRole('cell')
        .map((cell) => cell.getAttribute('data-label')),
    ).toEqual(['Player', 'Quality', 'Ping', 'Behind', 'Disconnects', 'Offline', 'Delayed orders']);
    expect(screen.getByRole('region', { name: 'Players and results' }).tabIndex).toBe(0);
    expect(screen.getByTestId('watch').getAttribute('href')).toBe(
      watchUrl(`http://localhost/api/v1/matches/${MATCH}/artifacts/replay`),
    );
  });

  it('clicking a match row navigates without reloading', async () => {
    // Flush the mocked fetches: the profile's initial match list is replaced
    // when its separate history request completes. Clicking a node returned
    // before that update can dispatch an event on an already detached link.
    await act(async () => {
      open(`/players/${ALICE}`);
    });
    fireEvent.click(screen.getAllByTestId('match-row')[0]!);
    expect(await screen.findByTestId('match-title')).toBeTruthy();
    expect(window.location.pathname).toBe(`/matches/${MATCH}`);
  });

  it('shows sign-in state and guards moderation', async () => {
    open('/admin');
    expect(await screen.findByText(/with a moderator account/)).toBeTruthy();
    for (const link of screen.getAllByRole('link', { name: 'Sign in' })) {
      expect(link.getAttribute('href')).toBe('/signin');
    }
    cleanup();
    me = {
      ...account(BOB, 'Bob'),
      role: 'user',
      status: 'active',
      identities: [],
      entitlements: [],
    };
    open('/admin');
    expect(await screen.findByText('This page is for moderators.')).toBeTruthy();
    expect(screen.getByTestId('account-chip').textContent).toContain('Bob');
    expect(screen.queryByRole('link', { name: 'Moderation' })).toBeNull();
  });

  it('deletes the account only after the name is typed', async () => {
    me = {
      ...account(BOB, 'Bob'),
      role: 'user',
      status: 'active',
      identities: [{ provider: 'local', linkedAt: NOW }],
      entitlements: [],
    };
    const deletes: unknown[] = [];
    type Fetch = (input: string, init?: RequestInit) => Promise<Response>;
    const stub = vi.mocked(globalThis.fetch as unknown as Fetch);
    const original = stub.getMockImplementation()!;
    stub.mockImplementation(async (input: string, init?: RequestInit) => {
      if (init?.method === 'DELETE' && input === '/api/v1/accounts/me') {
        deletes.push(JSON.parse(String(init.body)));
        me = undefined;
        return new Response(null, { status: 204 });
      }
      return original(input, init);
    });
    open('/account');
    expect(await screen.findByText('Delete my account')).toBeTruthy();
    // Download my data is a plain download link to the export endpoint.
    const download = screen.getByRole('link', { name: 'Download my data' });
    expect(download.getAttribute('href')).toBe('/api/v1/accounts/me/export');
    expect(download.hasAttribute('download')).toBe(true);
    const button = screen.getByRole('button', { name: 'Delete my account for good' });
    expect((button as HTMLButtonElement).disabled).toBe(true);
    const input = screen.getByLabelText(/Type your name, Bob, to confirm/);
    fireEvent.change(input, { target: { value: 'bob' } });
    expect((button as HTMLButtonElement).disabled).toBe(true);
    fireEvent.change(input, { target: { value: 'Bob' } });
    fireEvent.click(button);
    expect(await screen.findByText('Your account was deleted')).toBeTruthy();
    expect(deletes).toEqual([{ confirmDisplayName: 'Bob' }]);
  });
});

describe('LineChart', () => {
  it('labels series in a legend and an accessible table', () => {
    render(
      <LineChart
        title="Units"
        series={[
          {
            name: 'Red',
            color: 'red',
            points: [
              { x: 0, y: 1 },
              { x: 1, y: 3 },
            ],
          },
          {
            name: 'Blue',
            color: 'blue',
            points: [
              { x: 0, y: 2 },
              { x: 1, y: 2 },
            ],
          },
        ]}
      />,
    );
    expect(screen.getByRole('img', { name: 'Units' })).toBeTruthy();
    expect(screen.getAllByText('Red').length).toBeGreaterThan(0);
    expect(screen.getByRole('table').querySelectorAll('tbody tr')).toHaveLength(2);
  });

  it('says so when there is no data', () => {
    render(<LineChart title="Empty" series={[]} />);
    expect(screen.getByText('No data yet.')).toBeTruthy();
  });
});
