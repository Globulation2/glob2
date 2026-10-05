import { MusicReports } from '../music/Moderation.tsx';
import { SkinReports } from '../skins/Moderation.tsx';
// Minimal moderation (plan M8): accounts (search, rename, mute, ban), match
// lookup and map/skin report queues. Moderators may rename and mute;
// administrators may also ban. The API enforces the same rules.
import { useState, type FormEvent } from 'react';
import type { AdminAccount, MapReportInfo } from '@glob2/protocol';
import { api } from '../api.ts';
import { GameArt } from '../art.tsx';
import {
  Empty,
  ErrorNotice,
  Loaded,
  MapImage,
  MatchListView,
  PlayerLink,
} from '../components/common.tsx';
import { date, dateTime } from '../format.ts';
import { Link, useRouter } from '../router.tsx';
import { isModerator, useLoad, useSession } from '../state.tsx';

const TABS = [
  { id: 'music', name: 'Music reports' },
  { id: 'accounts', name: 'Accounts' },
  { id: 'matches', name: 'Matches' },
  { id: 'reports', name: 'Map reports' },
  { id: 'skins', name: 'Skin reports' },
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
          {error && <ErrorNotice error={error} />}
        </div>
      )}
    </div>
  );
}

function Accounts({ isAdmin }: { isAdmin: boolean }) {
  const { location } = useRouter();
  const [q, setQ] = useState(location.search.get('q') ?? '');
  const [query, setQuery] = useState(q);
  const load = useLoad((signal) => api.adminAccounts({ q: query }, signal), [query]);
  return (
    <>
      <form
        className="toolbar"
        role="search"
        onSubmit={(e) => {
          e.preventDefault();
          setQuery(q.trim());
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
        {(page) =>
          page.items.length === 0 ? (
            <Empty>No accounts match.</Empty>
          ) : (
            <div className="list">
              {page.items.map((a) => (
                <AccountRow key={`${a.id}-${a.updatedAt}`} initial={a} isAdmin={isAdmin} />
              ))}
            </div>
          )
        }
      </Loaded>
    </>
  );
}

function Matches() {
  const [q, setQ] = useState('');
  const [query, setQuery] = useState('');
  const [status, setStatus] = useState('');
  const load = useLoad(
    (signal) => api.adminMatches({ q: query, status, limit: 50 }, signal),
    [query, status],
  );
  return (
    <>
      <form
        className="toolbar"
        role="search"
        onSubmit={(e) => {
          e.preventDefault();
          setQuery(q.trim());
        }}
      >
        <input
          aria-label="Search matches"
          placeholder="Match id, account id, player name or relay"
          value={q}
          onChange={(e) => setQ(e.target.value)}
          style={{ flex: '1 1 260px' }}
        />
        <select aria-label="Status" value={status} onChange={(e) => setStatus(e.target.value)}>
          <option value="">Any status</option>
          <option value="starting">Starting</option>
          <option value="running">Running</option>
          <option value="ended">Ended</option>
          <option value="cancelled">Cancelled</option>
        </select>
        <button type="submit">Search</button>
      </form>
      <Loaded load={load}>
        {(page) => <MatchListView matches={page.items} empty="No matches found." />}
      </Loaded>
    </>
  );
}

function Report({ report, onChange }: { report: MapReportInfo; onChange: () => void }) {
  const [note, setNote] = useState('');
  const [error, setError] = useState<Error>();
  const act = async (action: () => Promise<unknown>) => {
    setError(undefined);
    try {
      await action();
      onChange();
    } catch (e) {
      setError(e as Error);
    }
  };
  const map = report.map;
  return (
    <div className="it" style={{ alignItems: 'flex-start' }} data-testid="admin-report">
      <MapImage src={map.latestVersion?.previewUrl} alt={map.title} size={64} />
      <div className="grow">
        <div>
          <Link to={`/maps/${map.id}`}>{map.title}</Link> by <PlayerLink account={map.owner} />{' '}
          {map.hidden && <span className="badge bad">hidden</span>}{' '}
          <span className="badge">{report.status}</span>
        </div>
        <div className="caption">
          {report.reason} · reported by <PlayerLink account={report.reporter} /> ·{' '}
          {dateTime(report.createdAt)}
        </div>
        {report.details && <p style={{ margin: '4px 0' }}>{report.details}</p>}
        {report.note && <p className="caption">Resolution: {report.note}</p>}
        {report.status === 'open' && (
          <div className="toolbar" style={{ marginTop: 6 }}>
            <input
              aria-label="Note"
              placeholder="Note (optional)"
              value={note}
              maxLength={2000}
              onChange={(e) => setNote(e.target.value)}
            />
            <button
              className="small danger"
              onClick={() =>
                void act(() =>
                  api.resolveReport(report.id, {
                    status: 'resolved',
                    hideMap: true,
                    hideReason: note || `Reported as ${report.reason}`,
                    ...(note ? { note } : {}),
                  }),
                )
              }
            >
              Hide map and resolve
            </button>
            <button
              className="small"
              onClick={() =>
                void act(() =>
                  api.resolveReport(report.id, { status: 'resolved', ...(note ? { note } : {}) }),
                )
              }
            >
              Resolve
            </button>
            <button
              className="small"
              onClick={() =>
                void act(() =>
                  api.resolveReport(report.id, { status: 'dismissed', ...(note ? { note } : {}) }),
                )
              }
            >
              Dismiss
            </button>
          </div>
        )}
        {map.hidden && (
          <button className="small" onClick={() => void act(() => api.unhideMap(map.id))}>
            Unhide map
          </button>
        )}
        {error && <ErrorNotice error={error} />}
      </div>
    </div>
  );
}

function Reports() {
  const [status, setStatus] = useState('open');
  const load = useLoad((signal) => api.adminReports({ status }, signal), [status]);
  return (
    <>
      <div
        className="seg"
        role="group"
        aria-label="Report status"
        style={{ marginBottom: 'var(--sp-4)' }}
      >
        {['open', 'resolved', 'dismissed', 'all'].map((s) => (
          <button
            key={s}
            className={status === s ? 'on' : ''}
            aria-pressed={status === s}
            onClick={() => setStatus(s)}
          >
            {s.charAt(0).toUpperCase() + s.slice(1)}
          </button>
        ))}
      </div>
      <Loaded load={load}>
        {(page) =>
          page.items.length === 0 ? (
            <Empty art="clearingFlag">No reports.</Empty>
          ) : (
            <div className="list">
              {page.items.map((r) => (
                <Report key={r.id} report={r} onChange={load.reload} />
              ))}
            </div>
          )
        }
      </Loaded>
    </>
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
  const current = TABS.find((t) => t.id === tab)?.id ?? 'accounts';
  return (
    <>
      <div className="page-head">
        <GameArt name="hospital" size={72} className="head-art" />
        <div className="grow">
          <h1>Moderation</h1>
          <p className="sub">
            Signed in as {account.displayName} ({account.role}). Every action is recorded.
          </p>
        </div>
      </div>
      <nav className="seg" aria-label="Moderation sections" style={{ marginBottom: 'var(--sp-4)' }}>
        {TABS.map((t) => (
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
      {current === 'reports' && <Reports />}
      {current === 'skins' && <SkinReports />}
      {current === 'music' && <MusicReports />}
    </>
  );
}
