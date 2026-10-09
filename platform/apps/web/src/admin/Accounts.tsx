import { statusLabel } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useState, type FormEvent } from 'react';
import type { AdminAccount } from '@glob2/protocol';
import { request, api } from '../api.ts';
import { Empty, ErrorNotice, Loaded, PlayerLink } from '../components/common.tsx';
import { date, dateTime } from '../format.ts';
import { useLoad, useSession } from '../state.tsx';
import { PageControls, useAdminFilters, useFilterDraft } from './filters.tsx';

const MUTES = [
  { minutes: 60, name: '1 hour' },
  { minutes: 1440, name: '1 day' },
  { minutes: 10080, name: '1 week' },
  { minutes: 43200, name: '30 days' },
];

function AccountRow({ initial, isAdmin }: { initial: AdminAccount; isAdmin: boolean }) {
  useLocale();
  const [account, setAccount] = useState(initial);
  const [open, setOpen] = useState(false);
  const [name, setName] = useState(initial.displayName);
  const [reason, setReason] = useState('');
  const [error, setError] = useState<Error>();
  const [busy, setBusy] = useState(false);
  const [now] = useState(() => Date.now());
  const { account: actor } = useSession();
  const [role, setRole] = useState(account.role);
  const [confirmation, setConfirmation] = useState('');
  const act = async (action: () => Promise<AdminAccount>) => {
    if (busy) return;
    setBusy(true);
    setError(undefined);
    try {
      setAccount(await action());
    } catch (e) {
      setError(e as Error);
    } finally {
      setBusy(false);
    }
  };
  const canRestrict =
    actor?.id !== account.id &&
    (actor?.role === 'admin' ? account.role !== 'admin' : account.role === 'user');
  const muted = account.mutedUntil && Date.parse(account.mutedUntil) > now;
  return (
    <div className="it" style={{ display: 'block' }} data-testid="admin-account">
      <div style={{ display: 'flex', gap: 10, alignItems: 'center', flexWrap: 'wrap' }}>
        <div className="grow">
          <PlayerLink account={account} />{' '}
          <span className="badge">{statusLabel(account.kind)}</span>{' '}
          {account.role !== 'user' && (
            <span className="badge gold">{statusLabel(account.role)}</span>
          )}{' '}
          {account.status === 'banned' && <span className="badge bad">{t('banned')}</span>}{' '}
          {muted && (
            <span className="badge warn">
              <RichMessage
                source={'muted until {slot0}'}
                slots={{ slot0: dateTime(account.mutedUntil) }}
              />
            </span>
          )}
          <div className="caption">
            <RichMessage
              source={'joined {slot0} · last seen {slot1} · {slot2}'}
              slots={{
                slot0: date(account.createdAt),
                slot1: dateTime(account.lastSeenAt),
                slot2:
                  account.identities.map((i) => i.provider).join(', ') || t('no sign-in methods'),
              }}
            />
          </div>
        </div>
        <button
          className="small"
          disabled={account.status === 'deleted'}
          onClick={() => setOpen(!open)}
          aria-expanded={open}
        >
          {account.status === 'deleted' ? t('Deleted') : open ? t('Close') : t('Moderate')}
        </button>
      </div>
      {open && account.status !== 'deleted' && (
        <fieldset disabled={busy} className="card" style={{ marginTop: 8 }}>
          <label className="field">
            {t('Reason (recorded in the audit log)')}
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
              aria-label={t('New display name')}
              value={name}
              maxLength={32}
              onChange={(e) => setName(e.target.value)}
            />
            <button
              className="small"
              type="submit"
              disabled={!name || name === account.displayName}
            >
              {t('Rename')}
            </button>
          </form>
          {!canRestrict && <p>{t('Restrictions and deletion require a lower-role account.')}</p>}
          <div className="toolbar">
            {canRestrict &&
              MUTES.map((m) => (
                <button
                  key={m.minutes}
                  className="small"
                  onClick={() =>
                    void act(() => api.adminMute(account.id, m.minutes, reason || undefined))
                  }
                >
                  <RichMessage source={'Mute {slot0}'} slots={{ slot0: m.name }} />
                </button>
              ))}
            {muted && (
              <button
                className="small"
                onClick={() => void act(() => api.adminMute(account.id, 0, reason || undefined))}
              >
                {t('Unmute')}
              </button>
            )}
          </div>
          {isAdmin && (canRestrict || account.status === 'banned') && (
            <div className="toolbar">
              <button
                className="small danger"
                onClick={() => {
                  const banned = account.status !== 'banned';
                  if (
                    !banned ||
                    window.confirm(
                      t('Ban {value0}? This signs them out everywhere.', {
                        value0: account.displayName,
                      }),
                    )
                  ) {
                    void act(() => api.adminBan(account.id, banned, reason || undefined));
                  }
                }}
              >
                {account.status === 'banned' ? t('Lift ban') : t('Ban')}
              </button>
            </div>
          )}
          {isAdmin && actor?.id !== account.id && account.kind === 'registered' && (
            <div className="toolbar">
              <label>
                {t('Role')}{' '}
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
                {t('Change role')}
              </button>
            </div>
          )}
          {isAdmin && canRestrict && (
            <details>
              <summary>{t('Delete account')}</summary>
              <p>
                {t(
                  'Deletes sign-in identities, sessions, personal information and owned content. Match history remains anonymized. This cannot be undone.',
                )}
              </p>
              <label>
                {t('Type ')}
                <bdi dir="auto">{account.displayName}</bdi> {t(' to confirm')}{' '}
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
                {t('Delete account')}
              </button>
            </details>
          )}
          {busy && <p role="status">{t('Saving account changes…')}</p>}
          {error && <ErrorNotice error={error} />}
        </fieldset>
      )}
    </div>
  );
}

export function Accounts({ isAdmin }: { isAdmin: boolean }) {
  useLocale();
  const { values, set } = useAdminFilters();
  const [q, setQ] = useFilterDraft(values['q'] ?? '');
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
          aria-label={t('Search accounts')}
          placeholder={t('Name, account id or email')}
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <button type="submit">{t('Search')}</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            {page.items.length === 0 ? (
              <Empty>{t('No accounts match.')}</Empty>
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
