import { getLocale } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { ProfilePhoto } from '../components/ProfilePhoto.tsx';
import { useState, type FormEvent } from 'react';
import type { SelfAccount } from '@glob2/protocol';
import { ACCOUNT_EXPORT_PATH, api } from '../api.ts';
import { ErrorNotice } from '../components/common.tsx';
import { Link } from '../router.tsx';
import { useSession } from '../state.tsx';

/**
 * The signed-in player's own account: what it is, downloading everything the
 * instance stores about it, and deleting it for good. The game's settings
 * ("Download or delete my data") open this page.
 */
export function Account() {
  useLocale();
  const { account, refresh } = useSession();
  const [deleted, setDeleted] = useState(false);
  if (deleted) {
    return (
      <div className="card" role="status">
        <h1>{t('Your account was deleted')}</h1>
        <p>
          {t(
            'You are signed out everywhere. Your name is gone from your past matches, which now show “Deleted player”. Thanks for playing.',
          )}
        </p>
        <Link className="btn primary" to="/">
          {t('Back to the home page')}
        </Link>
      </div>
    );
  }
  if (account === undefined) return null;
  if (!account) {
    return (
      <div className="card">
        <h1>{t('Your account')}</h1>
        <p>{t('Sign in to see your account or to delete it.')}</p>
        <a className="btn primary" href="/signin">
          {t('Sign in')}
        </a>
      </div>
    );
  }
  return (
    <>
      <div className="page-head">
        <div className="grow">
          <h1>{t('Your account')}</h1>
          <p className="sub">
            <RichMessage
              source={'{slot0} · {slot1} since {slot2}'}
              slots={{
                slot0: account.displayName,
                slot1: account.kind === 'guest' ? t('Guest') : t('Registered'),
                slot2: new Date(account.createdAt).toLocaleDateString(getLocale()),
              }}
            />
          </p>
        </div>
        <Link className="btn" to={`/players/${account.id}`}>
          {t('Your profile')}
        </Link>
      </div>
      <div className="account-settings">
        {account.kind === 'registered' && <ProfilePhoto account={account} onSaved={refresh} />}
        <div className="card">
          <h2 className="card-title">{t('Sign-in methods')}</h2>
          {account.identities.length === 0 ? (
            <p>{t('None: this guest account lives on the device that created it.')}</p>
          ) : (
            <ul>
              {account.identities.map((identity) => (
                <li key={identity.provider}>
                  {identity.provider === 'local' ? t('Username and password') : identity.provider}
                  {identity.email ? ` (${identity.email})` : ''}
                </li>
              ))}
            </ul>
          )}
        </div>
        <div className="card">
          <h2 className="card-title">{t('Download my data')}</h2>
          <p>
            {t(
              'A JSON file with everything this server stores about your account: your profile, sign-in methods (without passwords or keys), matches, ratings, rooms and chat, matchmaking, maps and moderation records.',
            )}
          </p>
          <a className="btn" href={ACCOUNT_EXPORT_PATH} download>
            {t('Download my data')}
          </a>
        </div>
        <div className="card">
          <h2 className="card-title">{t('Hive Mind')}</h2>
          <p>{t('Manage the credits for your in-game AI commander.')}</p>
          <Link className="btn" to="/commander">
            {t('Hive Mind credits')}
          </Link>
        </div>
        <DeleteAccount
          account={account}
          onDeleted={() => {
            setDeleted(true);
            refresh();
          }}
        />
      </div>
    </>
  );
}

function DeleteAccount({ account, onDeleted }: { account: SelfAccount; onDeleted: () => void }) {
  useLocale();
  const [typed, setTyped] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<Error>();
  const matches = typed.trim() === account.displayName;
  const submit = async (event: FormEvent) => {
    event.preventDefault();
    if (!matches) return;
    setBusy(true);
    setError(undefined);
    try {
      await api.deleteAccount(typed.trim());
      onDeleted();
    } catch (e) {
      setError(e as Error);
      setBusy(false);
    }
  };
  return (
    <form className="card" onSubmit={submit} aria-labelledby="delete-title">
      <h2 className="card-title" id="delete-title">
        {t('Delete my account')}
      </h2>
      <p>{t('This cannot be undone. Deleting your account:')}</p>
      <ul>
        <li>
          {t('signs you out on every device and removes your sign-in methods and e-mail address;')}
        </li>
        <li>
          {t('replaces your name with “Deleted player” in your past matches, chat and records;')}
        </li>
        <li>
          {t('deletes your maps, likes, uploads and your rating’s place on the leaderboards.')}
        </li>
      </ul>
      <p className="muted">
        {t(
          'Matches you played stay in other players’ history, and their replay files still contain the name you had in the game.',
        )}
      </p>
      <label className="field">
        {t('Type your name, ')}
        <bdi dir="auto">{account.displayName}</bdi>
        {t(', to confirm')}
        <input
          dir="auto"
          value={typed}
          onChange={(e) => setTyped(e.target.value)}
          autoComplete="off"
          spellCheck={false}
          maxLength={64}
          aria-describedby="delete-title"
        />
      </label>
      {error && <ErrorNotice error={error} />}
      <button className="danger" type="submit" disabled={!matches || busy}>
        {busy ? t('Deleting…') : t('Delete my account for good')}
      </button>
    </form>
  );
}
