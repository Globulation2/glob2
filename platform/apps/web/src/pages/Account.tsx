import { useState, type FormEvent } from 'react';
import type { SelfAccount } from '@glob2/protocol';
import { api } from '../api.ts';
import { ErrorNotice } from '../components/common.tsx';
import { Link } from '../router.tsx';
import { useSession } from '../state.tsx';

/**
 * The signed-in player's own account: what it is, and deleting it for good.
 * The game's settings ("Delete my account") open this page.
 */
export function Account() {
  const { account, refresh } = useSession();
  const [deleted, setDeleted] = useState(false);
  if (deleted) {
    return (
      <div className="card" role="status">
        <h1>Your account was deleted</h1>
        <p>
          You are signed out everywhere. Your name is gone from your past matches, which now show
          “Deleted player”. Thanks for playing.
        </p>
        <Link className="btn primary" to="/">
          Back to the home page
        </Link>
      </div>
    );
  }
  if (account === undefined) return null;
  if (!account) {
    return (
      <div className="card">
        <h1>Your account</h1>
        <p>Sign in to see your account or to delete it.</p>
        <a className="btn primary" href="/signin">
          Sign in
        </a>
      </div>
    );
  }
  return (
    <>
      <div className="page-head">
        <div className="grow">
          <h1>Your account</h1>
          <p className="sub">
            {account.displayName} · {account.kind === 'guest' ? 'Guest' : 'Registered'} since{' '}
            {new Date(account.createdAt).toLocaleDateString()}
          </p>
        </div>
        <Link className="btn" to={`/players/${account.id}`}>
          Your profile
        </Link>
      </div>
      <div className="card">
        <h2 className="card-title">Sign-in methods</h2>
        {account.identities.length === 0 ? (
          <p>None: this guest account lives on the device that created it.</p>
        ) : (
          <ul>
            {account.identities.map((identity) => (
              <li key={identity.provider}>
                {identity.provider === 'local' ? 'Username and password' : identity.provider}
                {identity.email ? ` (${identity.email})` : ''}
              </li>
            ))}
          </ul>
        )}
      </div>
      <DeleteAccount
        account={account}
        onDeleted={() => {
          setDeleted(true);
          refresh();
        }}
      />
    </>
  );
}

function DeleteAccount({ account, onDeleted }: { account: SelfAccount; onDeleted: () => void }) {
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
        Delete my account
      </h2>
      <p>This cannot be undone. Deleting your account:</p>
      <ul>
        <li>signs you out on every device and removes your sign-in methods and e-mail address;</li>
        <li>replaces your name with “Deleted player” in your past matches, chat and records;</li>
        <li>deletes your maps, likes, uploads and your rating’s place on the leaderboards.</li>
      </ul>
      <p className="muted">
        Matches you played stay in other players’ history, and their replay files still contain the
        name you had in the game.
      </p>
      <label className="field">
        Type your name, {account.displayName}, to confirm
        <input
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
        {busy ? 'Deleting…' : 'Delete my account for good'}
      </button>
    </form>
  );
}
