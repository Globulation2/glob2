import { MusicReports } from '../music/Moderation.tsx';
import { SkinReports } from '../skins/Moderation.tsx';
import { Operations } from './Operations.tsx';
import { Overview } from './Overview.tsx';
import { Finances } from './Finances.tsx';
import { UnifiedReports, Content, Audit, PageControls, useAdminFilters } from './Moderation.tsx';
// Central administration; legacy music and skin report URLs remain supported.
import { useState, type FormEvent } from 'react';
import type { AdminAccount } from '@glob2/protocol';
import { request, api } from '../api.ts';
import { GameArt } from '../art.tsx';
import { Empty, ErrorNotice, Loaded, MatchListView, PlayerLink } from '../components/common.tsx';
import { date, dateTime } from '../format.ts';
import { Link } from '../router.tsx';
import { isModerator, useLoad, useSession } from '../state.tsx';

const TABS = [
  { id: 'overview', name: 'Overview' },
  { id: 'finances', name: 'Finances' },
  { id: 'accounts', name: 'Accounts' },
  { id: 'matches', name: 'Matches' },
  { id: 'reports', name: 'Reports' },
  { id: 'content', name: 'Content' },
  { id: 'audit', name: 'Audit' },
  { id: 'operations', name: 'Operations' },
];

const MUTES = [
  { minutes: 60, name: '1 hour' },
  { minutes: 1440, name: '1 day' },
  { minutes: 10080, name: '1 week' },
  { minutes: 43200, name: '30 days' },
];

function AccountRow({ initial, isAdmin }: { initial: AdminAccount; isAdmin: boolean }) {
  const [account, setAccount] = useState(initial);
  const [open, setOpen] = useState(false);
  const [name, setName] = useState(initial.displayName);
  const [reason, setReason] = useState('');
  const [error, setError] = useState<Error>();
  const [now] = useState(() => Date.now());
  const { account: actor } = useSession();
  const [role, setRole] = useState(account.role);
  const [confirmation, setConfirmation] = useState('');
  const act = async (action: () => Promise<AdminAccount>) => {
    setError(undefined);
    try {
      setAccount(await action());
    } catch (e) {
      setError(e as Error);
    }
  };
  const muted = account.mutedUntil && Date.parse(account.mutedUntil) > now;
  return (
    <div className="it" style={{ display: 'block' }} data-testid="admin-account">
      <div style={{ display: 'flex', gap: 10, alignItems: 'center', flexWrap: 'wrap' }}>
        <div className="grow">
          <PlayerLink account={account} /> <span className="badge">{account.kind}</span>{' '}
          {account.role !== 'user' && <span className="badge gold">{account.role}</span>}{' '}
          {account.status === 'banned' && <span className="badge bad">banned</span>}{' '}
          {muted && <span className="badge warn">muted until {dateTime(account.mutedUntil)}</span>}
          <div className="caption">
            joined {date(account.createdAt)} · last seen {dateTime(account.lastSeenAt)} ·{' '}
            {account.identities.map((i) => i.provider).join(', ') || 'no sign-in methods'}
          </div>
        </div>
        <button className="small" onClick={() => setOpen(!open)} aria-expanded={open}>
          {open ? 'Close' : 'Moderate'}
        </button>
      </div>
      {open && (
        <div className="card" style={{ marginTop: 8 }}>
          <label className="field">
            Reason (recorded in the audit log)
            <input value={reason} maxLength={500} onChange={(e) => setReason(e.target.value)} />
          </label>
          <form
            className="toolbar"
            onSubmit={(e: FormEvent) => {
              e.preventDefault();
              void act(() => api.adminRename(account.id, name, reason || undefined));
            }}
          >
            <input
              aria-label="New display name"
              value={name}
              maxLength={32}
              onChange={(e) => setName(e.target.value)}
            />
            <button
              className="small"
              type="submit"
              disabled={!name || name === account.displayName}
            >
              Rename
            </button>
          </form>
          <div className="toolbar">
            {MUTES.map((m) => (
              <button
                key={m.minutes}
                className="small"
                onClick={() =>
                  void act(() => api.adminMute(account.id, m.minutes, reason || undefined))
                }
              >
                Mute {m.name}
              </button>
            ))}
            {muted && (
              <button
                className="small"
                onClick={() => void act(() => api.adminMute(account.id, 0, reason || undefined))}
              >
                Unmute
              </button>
            )}
          </div>
          {isAdmin && (
            <div className="toolbar">
              <button
                className="small danger"
                onClick={() => {
                  const banned = account.status !== 'banned';
                  if (
                    !banned ||
                    window.confirm(`Ban ${account.displayName}? This signs them out everywhere.`)
                  ) {
                    void act(() => api.adminBan(account.id, banned, reason || undefined));
                  }
                }}
              >
                {account.status === 'banned' ? 'Lift ban' : 'Ban'}
              </button>
            </div>
          )}
          {isAdmin && actor?.id !== account.id && account.kind === 'registered' && (
            <div className="toolbar">
              <label>
                Role{' '}
                <select value={role} onChange={(e) => setRole(e.target.value as typeof role)}>
                  {['user', 'moderator', 'admin'].map((r) => (
                    <option key={r}>{r}</option>
                  ))}
                </select>
              </label>
              <button
                disabled={role === account.role || !reason.trim()}
                onClick={() =>
                  void act(() =>
                    request<AdminAccount>('POST', `/api/v1/admin/accounts/${account.id}/role`, {
                      body: { role, reason },
                    }),
                  )
                }
              >
                Change role
              </button>
            </div>
          )}
          {isAdmin && actor?.id !== account.id && (
            <details>
              <summary>Delete account</summary>
              <p>
                Deletes sign-in identities, sessions, personal information and owned content. Match
                history remains anonymized. This cannot be undone.
              </p>
              <label>
                Type {account.displayName} to confirm{' '}
                <input value={confirmation} onChange={(e) => setConfirmation(e.target.value)} />
              </label>
              <button
                className="danger"
                disabled={confirmation !== account.displayName || !reason.trim()}
                onClick={() =>
                  void act(async () => {
                    await request('DELETE', `/api/v1/admin/accounts/${account.id}`, {
                      query: { reason },
                    });
                    setOpen(false);
                    return { ...account, status: 'deleted', displayName: 'Deleted player' };
                  })
                }
              >
                Delete account
              </button>
            </details>
          )}
          {error && <ErrorNotice error={error} />}
        </div>
      )}
    </div>
  );
}

function Accounts({ isAdmin }: { isAdmin: boolean }) {
  const { values, set } = useAdminFilters();
  const [q, setQ] = useState(values['q'] ?? '');
  const load = useLoad((signal) => api.adminAccounts(values, signal), [JSON.stringify(values)]);
  return (
    <>
      <form
        className="toolbar"
        role="search"
        onSubmit={(e) => {
          e.preventDefault();
          set({ q: q.trim() });
        }}
      >
        <input
          aria-label="Search accounts"
          placeholder="Name, account id or email"
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <button type="submit">Search</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            {page.items.length === 0 ? (
              <Empty>No accounts match.</Empty>
            ) : (
              <div className="list">
                {page.items.map((a) => (
                  <AccountRow key={`${a.id}-${a.updatedAt}`} initial={a} isAdmin={isAdmin} />
                ))}
              </div>
            )}
            <PageControls nextCursor={page.nextCursor} />
          </>
        )}
      </Loaded>
    </>
  );
}

function Matches() {
  const { values, set } = useAdminFilters();
  const [q, setQ] = useState(values['q'] ?? '');
  const load = useLoad((signal) => api.adminMatches(values, signal), [JSON.stringify(values)]);
  const { account } = useSession();
  return (
    <>
      <form
        className="toolbar"
        role="search"
        onSubmit={(e) => {
          e.preventDefault();
          set({ q: q.trim() });
        }}
      >
        <input
          aria-label="Search matches"
          placeholder="Match id, account id, player name or relay"
          value={q}
          onChange={(e) => setQ(e.target.value)}
          style={{ flex: '1 1 260px' }}
        />
        <select
          aria-label="Status"
          value={values['status'] ?? ''}
          onChange={(e) => set({ status: e.target.value })}
        >
          <option value="">Any status</option>
          <option value="starting">Starting</option>
          <option value="running">Running</option>
          <option value="ended">Ended</option>
          <option value="cancelled">Cancelled</option>
        </select>
        <label>
          Verification{' '}
          <select
            value={values['verification'] ?? ''}
            onChange={(e) => set({ verification: e.target.value })}
          >
            <option value="">Any</option>
            {['pending', 'failed', 'verified', 'diverged', 'unverifiable', 'not_applicable'].map(
              (v) => (
                <option key={v}>{v}</option>
              ),
            )}
          </select>
        </label>
        <button type="submit">Search</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            <MatchListView matches={page.items} empty="No matches found." />
            {account?.role === 'admin' &&
              page.items
                .filter((m) => m.status === 'ended')
                .map((m) => <Reverify key={m.id} id={m.id} reload={load.reload} />)}
            <PageControls nextCursor={page.nextCursor} />
          </>
        )}
      </Loaded>
    </>
  );
}

function Reverify({ id, reload }: { id: string; reload: () => void }) {
  const [reason, setReason] = useState(''),
    [force, setForce] = useState(false),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('');
  async function act() {
    if (force && !window.confirm(`Force re-verification of ${id}?`)) return;
    setBusy(true);
    try {
      await request('POST', `/api/v1/admin/matches/${id}/reverify`, { body: { force, reason } });
      reload();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  }
  return (
    <details>
      <summary>Re-verify {id}</summary>
      <label>
        Reason <input value={reason} onChange={(e) => setReason(e.target.value)} maxLength={2000} />
      </label>
      <label>
        <input type="checkbox" checked={force} onChange={(e) => setForce(e.target.checked)} />
        Force re-verification
      </label>
      <button disabled={busy || !reason.trim()} onClick={() => void act()}>
        Re-verify
      </button>
      {error && <p role="alert">{error}</p>}
    </details>
  );
}

export function Admin({ tab }: { tab: string | undefined }) {
  const { account } = useSession();
  if (account === undefined) return null;
  if (!account) {
    return (
      <div className="notice">
        <a href="/signin">Sign in</a> with a moderator account.
      </div>
    );
  }
  if (!isModerator(account)) {
    return <div className="notice error">This page is for moderators.</div>;
  }
  const current =
    (['music', 'skins'].includes(tab ?? '') ? tab : TABS.find((t) => t.id === tab)?.id) ??
    (account.role === 'admin' ? 'overview' : 'reports');
  return (
    <>
      <div className="page-head">
        <GameArt name="clearingFlag" size={72} className="head-art" />
        <div className="grow">
          <h1>Administration</h1>
          <p className="sub">
            Signed in as {account.displayName} ({account.role}). Every action is recorded.
          </p>
        </div>
      </div>
      <nav className="seg" aria-label="Moderation sections" style={{ marginBottom: 'var(--sp-4)' }}>
        {TABS.filter(
          (t) => !['overview', 'operations', 'finances'].includes(t.id) || account.role === 'admin',
        ).map((t) => (
          <Link
            key={t.id}
            to={`/admin/${t.id}`}
            aria-current={current === t.id ? 'page' : undefined}
            className={current === t.id ? 'on' : ''}
          >
            {t.name}
          </Link>
        ))}
      </nav>
      {current === 'accounts' && <Accounts isAdmin={account.role === 'admin'} />}
      {current === 'matches' && <Matches />}
      {current === 'reports' && <UnifiedReports />}
      {current === 'skins' && <SkinReports />}
      {current === 'music' && <MusicReports />}
      {current === 'content' && <Content />}
      {current === 'audit' && <Audit />}
      {current === 'overview' &&
        (account.role === 'admin' ? <Overview /> : <p>Administrator access is required.</p>)}
      {current === 'finances' &&
        (account.role === 'admin' ? <Finances /> : <p>Administrator access is required.</p>)}
      {current === 'operations' &&
        (account.role === 'admin' ? <Operations /> : <p>Administrator access is required.</p>)}
    </>
  );
}
