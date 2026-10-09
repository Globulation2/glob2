import { MessageError } from '../i18n.tsx';
import { message as sourceMessage, displayMessage } from '../i18n.tsx';
import { getLocale } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useEffect, useState } from 'react';
import { ApiError, request } from '../api.ts';
interface Account {
  enabled: boolean;
  available: number;
  balance: number;
  reserved: number;
  usage: { id: string; amount: string; kind: string; created_at: string }[];
  rate: {
    version: string;
    input: number;
    cachedInput: number;
    cacheWrite?: number;
    output: number;
  } | null;
  packs: { id: string; credits: number; amount: number; currency: string }[];
}
export function CommanderCredits() {
  useLocale();
  const [account, setAccount] = useState<Account>();
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    let alive = true;
    const refresh = () => {
      void request<Account>('GET', '/api/v1/hive/account')
        .then((a) => {
          if (alive) {
            setAccount(a);
            setError('');
          }
        })
        .catch((error: unknown) => {
          if (alive)
            setError(
              error instanceof ApiError && error.status === 401
                ? t('Sign in to view your commander and credits.')
                : t('Your account could not be refreshed. Please try again shortly.'),
            );
        });
    };
    refresh();
    const timer = setInterval(refresh, 5000);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, []);
  async function buy(pack: string) {
    setBusy(true);
    setError('');
    try {
      const { url } = await request<{ url: string }>('POST', '/api/v1/hive/checkout', {
        body: { pack },
      });
      const target = new URL(url);
      if (target.protocol !== 'https:' || target.hostname !== 'checkout.stripe.com')
        throw new MessageError('Invalid checkout destination');
      window.location.assign(url);
    } catch {
      setError(sourceMessage('Checkout could not be opened. Please try again.'));
      setBusy(false);
    }
  }
  return (
    <section aria-label={t('Commander account')}>
      <h1>{t('Hive Mind')}</h1>
      <p>{t('Give orders. Receive reports. Lead your colony.')}</p>
      <p>
        {t(
          'Configure Hive Mind in Settings → Online. In a match, press Ctrl+Enter to give an order. Assistance is permitted in ranked matches.',
        )}
      </p>
      {error && <p role="alert">{displayMessage(error)}</p>}
      {account && (
        <>
          <h2>
            <RichMessage
              source={'{slot0} credits available'}
              slots={{ slot0: account.available.toLocaleString(getLocale()) }}
              singular={'{slot0} credit available'}
              count={Number(account.available)}
            />
          </h2>
          {!account.enabled && <p>{t('Hive Mind is not enabled on this instance.')}</p>}
          <p>
            {t(
              'Credits pay for your commander’s thinking and follow-ups. Standing orders keep running when credits run out.',
            )}
          </p>
          <p>
            <RichMessage
              source={'{slot0} credits reserved for work in progress.'}
              slots={{ slot0: account.reserved.toLocaleString(getLocale()) }}
              singular={'{slot0} credit reserved for work in progress.'}
              count={Number(account.reserved)}
            />
          </p>
          {account.rate && (
            <details>
              <summary>{t('Detailed usage rates')}</summary>
              <p>
                <RichMessage
                  source={
                    'Rate {slot0}: {slot1} credits per million input tokens, {slot2} for cached input, {slot3} for cache writes, and {slot4} for output including reasoning. Repairs and follow-ups use the same rates.'
                  }
                  slots={{
                    slot0: account.rate.version,
                    slot1: account.rate.input,
                    slot2: account.rate.cachedInput,
                    slot3: account.rate.cacheWrite ?? account.rate.input,
                    slot4: account.rate.output,
                  }}
                  singular={
                    'Rate {slot0}: {slot1} credit per million input tokens, {slot2} for cached input, {slot3} for cache writes, and {slot4} for output including reasoning. Repairs and follow-ups use the same rates.'
                  }
                  count={Number(account.rate.input)}
                />
              </p>
            </details>
          )}
          <h2>{t('Recent activity')}</h2>
          {!account.usage.length && <p>{t('No commander activity yet.')}</p>}
          <ul>
            {account.usage.map((entry) => (
              <li key={entry.id}>
                <RichMessage
                  source={'{slot0}: {slot1} · {slot2} credits'}
                  slots={{
                    slot0: new Date(entry.created_at).toLocaleString(getLocale()),
                    slot1:
                      entry.kind === 'usage'
                        ? t('Commander work')
                        : entry.kind === 'purchase'
                          ? t('Credit purchase')
                          : entry.kind === 'grant'
                            ? t('Development credits')
                            : t('Account adjustment'),
                    slot2: Number(entry.amount).toLocaleString(getLocale()),
                  }}
                  singular={'{slot0}: {slot1} · {slot2} credit'}
                  count={Number(Number(entry.amount))}
                />
              </li>
            ))}
          </ul>
          {account.packs.map((p) => (
            <button disabled={busy} key={p.id} onClick={() => void buy(p.id)}>
              <RichMessage
                source={'{slot0} credits — {slot1}'}
                slots={{
                  slot0: p.credits.toLocaleString(getLocale()),
                  slot1: new Intl.NumberFormat(getLocale(), {
                    style: 'currency',
                    currency: p.currency,
                  }).format(p.amount / 100),
                }}
                singular={'{slot0} credit — {slot1}'}
                count={Number(p.credits)}
              />
            </button>
          ))}
          {!account.packs.length && (
            <p>{t('Credit purchases are not available on this instance.')}</p>
          )}
          <p>
            {t(
              'After payment, your balance updates once payment is confirmed. You can return to your colony while it is processed.',
            )}
          </p>
        </>
      )}
    </section>
  );
}
