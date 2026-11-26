import { useState } from 'react';
import type { SelfAccount } from '@glob2/protocol';
import { api } from '../api.ts';
import { t, useLocale } from '../i18n.tsx';
import { ErrorNotice } from './common.tsx';

/**
 * The one-time age question for accounts that have not answered it in the game
 * (docs/multiplayer/identity.md#age-bands). Only the band is kept, never a
 * birthdate. Hidden once the account is 13 plus; an under-13 account sees why
 * chat and sharing are off.
 */
export function AgeBand({ account, onSaved }: { account: SelfAccount; onSaved: () => void }) {
  useLocale();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<Error>();
  const band = account.ageBand ?? 'unknown';
  if (band === '13_plus') return null;
  const answer = (value: 'under_13' | '13_plus') => {
    setBusy(true);
    setError(undefined);
    api
      .setAgeBand(value)
      .then(onSaved, (e: unknown) => setError(e instanceof Error ? e : new Error(String(e))))
      .finally(() => setBusy(false));
  };
  return (
    <div className="card" id="age">
      <h2 className="card-title">{t('Your age')}</h2>
      {band === 'under_13' ? (
        <p>
          {t(
            'You said you are under 13, so chat and sharing are off. A parent can contact us to change this.',
          )}
        </p>
      ) : (
        <>
          <p>
            {t(
              'Chat, sharing content beyond private, choosing a name and uploading a photo are for players aged 13 or over. Only your answer is kept, never your birthday.',
            )}
          </p>
          <div className="toolbar">
            <button className="btn" disabled={busy} onClick={() => answer('under_13')}>
              {t('Under 13')}
            </button>
            <button className="btn" disabled={busy} onClick={() => answer('13_plus')}>
              {t('13 or over')}
            </button>
          </div>
        </>
      )}
      {error && <ErrorNotice error={error} />}
    </div>
  );
}
