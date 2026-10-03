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
                ? 'Sign in to view your commander and credits.'
                : 'Your account could not be refreshed. Please try again shortly.',
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
        throw new Error('Invalid checkout destination');
      window.location.assign(url);
    } catch {
      setError('Checkout could not be opened. Please try again.');
      setBusy(false);
    }
  }
  return (
    <section aria-label="Commander account">
      <h1>Hive Mind</h1>
      <p>Give orders. Receive reports. Lead your colony.</p>
      <p>
        Configure Hive Mind in Settings → Online. In a match, press Ctrl+Enter to give an order.
        Assistance is permitted in ranked matches.
      </p>
      {error && <p role="alert">{error}</p>}
      {account && (
        <>
          <h2>{account.available.toLocaleString()} credits available</h2>
          {!account.enabled && <p>Hive Mind is not enabled on this instance.</p>}
          <p>
            Credits pay for your commander’s thinking and follow-ups. Standing orders keep running
            when credits run out.
          </p>
          <p>{account.reserved.toLocaleString()} credits reserved for work in progress.</p>
          {account.rate && (
            <details>
              <summary>Detailed usage rates</summary>
              <p>
                Rate {account.rate.version}: {account.rate.input} credits per million input tokens,{' '}
                {account.rate.cachedInput} for cached input,{' '}
                {account.rate.cacheWrite ?? account.rate.input} for cache writes, and{' '}
                {account.rate.output} for output including reasoning. Repairs and follow-ups use the
                same rates.
              </p>
            </details>
          )}
          <h2>Recent activity</h2>
          {!account.usage.length && <p>No commander activity yet.</p>}
          <ul>
            {account.usage.map((entry) => (
              <li key={entry.id}>
                {new Date(entry.created_at).toLocaleString()}:{' '}
                {entry.kind === 'usage'
                  ? 'Commander work'
                  : entry.kind === 'purchase'
                    ? 'Credit purchase'
                    : entry.kind === 'grant'
                      ? 'Development credits'
                      : 'Account adjustment'}{' '}
                · {Number(entry.amount).toLocaleString()} credits
              </li>
            ))}
          </ul>
          {account.packs.map((p) => (
            <button disabled={busy} key={p.id} onClick={() => void buy(p.id)}>
              {p.credits.toLocaleString()} credits —{' '}
              {new Intl.NumberFormat(undefined, { style: 'currency', currency: p.currency }).format(
                p.amount / 100,
              )}
            </button>
          ))}
          {!account.packs.length && <p>Credit purchases are not available on this instance.</p>}
          <p>
            After payment, your balance updates once payment is confirmed. You can return to your
            colony while it is processed.
          </p>
        </>
      )}
    </section>
  );
}
