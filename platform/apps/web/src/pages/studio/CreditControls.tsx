import { price, type Wallet } from './types.ts';

export function CreditControls({
  wallet,
  busy,
  buy,
  close,
}: {
  wallet?: Wallet;
  busy: boolean;
  buy: (pack: string) => void;
  close: () => void;
}) {
  return (
    <section className="ms-credit-panel" aria-label="Manage credits">
      <div>
        <h2>Your next great map</h2>
        <p>
          {wallet?.available ?? 0} available · {wallet?.reserved ?? 0} reserved. Discussion requires
          an available credit; each delivered map or revision costs 1 credit. Failed generations
          return the credit.
        </p>
      </div>
      <div>
        {wallet?.packs.map((p) => (
          <button
            key={p.id}
            className="primary"
            disabled={busy || !wallet.enabled}
            onClick={() => buy(p.id)}
          >
            Buy {p.credits} credits · {price(p)}
          </button>
        ))}
        {!wallet?.packs.length && <p>Credit purchases are currently unavailable.</p>}
        <details>
          <summary>Credit activity</summary>
          {wallet?.usage.map((entry) => (
            <p key={entry.id}>
              {new Date(entry.created_at).toLocaleString()} · {entry.kind} · {entry.amount}
            </p>
          ))}
        </details>
      </div>
      <button aria-label="Close credits" onClick={close}>
        ×
      </button>
    </section>
  );
}
