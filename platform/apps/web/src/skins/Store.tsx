import { useState } from 'react';
import { request } from '../api.ts';
import { useLoad, useSession } from '../state.tsx';

type Product = {
  sku: string;
  name: string;
  available: boolean;
  amount: number | null;
  currency: string | null;
};
type Purchase = { id: string; sku: string; status: string; createdAt: string };
const ZERO_DECIMAL = new Set([
  'bif',
  'clp',
  'djf',
  'gnf',
  'jpy',
  'kmf',
  'krw',
  'mga',
  'pyg',
  'rwf',
  'vnd',
  'vuv',
  'xaf',
  'xof',
  'xpf',
]);
function price(product: Product) {
  if (product.amount === null || !product.currency) return 'Not available yet';
  return new Intl.NumberFormat(undefined, { style: 'currency', currency: product.currency }).format(
    product.amount / (ZERO_DECIMAL.has(product.currency) ? 1 : 100),
  );
}
export function SkinStore({
  onChange,
  beforeCheckout,
}: {
  onChange: () => void;
  beforeCheckout?: () => boolean;
}) {
  const { account } = useSession();
  const [busy, setBusy] = useState(false),
    [message, setMessage] = useState(() =>
      new URLSearchParams(window.location.search).has('purchase')
        ? 'Checkout returned. Check the payment below to confirm your purchase.'
        : '',
    );
  const products = useLoad(
    (signal) =>
      request<{ testMode: boolean; items: Product[] }>('GET', '/api/v1/skins/products', { signal }),
    [],
  );
  const purchases = useLoad(
    (signal) =>
      account
        ? request<{ items: Purchase[] }>('GET', '/api/v1/skins/purchases', { signal })
        : Promise.resolve({ items: [] }),
    [account?.id],
  );
  async function buy(sku: string) {
    if (beforeCheckout && !beforeCheckout()) {
      setMessage('Save your draft before leaving for checkout. Device storage is unavailable.');
      return;
    }
    setBusy(true);
    try {
      const checkout = await request<{ url: string }>('POST', '/api/v1/skins/checkout', {
        body: { sku, requestId: crypto.randomUUID() },
      });
      if (new URL(checkout.url).origin !== 'https://checkout.stripe.com')
        throw new Error('Unexpected checkout address.');
      window.location.assign(checkout.url);
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Checkout failed.');
      setBusy(false);
    }
  }
  async function reconcile(id: string) {
    setBusy(true);
    try {
      const result = await request<{ status: string }>(
        'POST',
        '/api/v1/skins/purchases/reconcile',
        { body: { purchaseId: id } },
      );
      setMessage(
        result.status === 'paid'
          ? 'Purchase confirmed. Your skin is ready to equip.'
          : `Payment status: ${result.status}.`,
      );
      purchases.reload();
      onChange();
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Could not check payment.');
    } finally {
      setBusy(false);
    }
  }
  return (
    <section aria-label="Skin shop">
      <h2>Skin shop</h2>
      <p>One-time purchases stay with your account. Preview and paint before buying.</p>
      {products.status === 'error' && <p role="alert">{products.error.message}</p>}
      {products.status === 'ready' && (
        <>
          {products.data.testMode && <p>Test store — real-money checkout is not enabled.</p>}
          <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
            {products.data.items.map((product) => (
              <article
                key={product.sku}
                style={{ padding: 16, border: '1px solid currentColor', borderRadius: 12 }}
              >
                <h3>{product.name}</h3>
                <p>{price(product)}</p>
                <button
                  disabled={busy || !product.available || account?.kind !== 'registered'}
                  onClick={() => void buy(product.sku)}
                >
                  Buy {product.name}
                </button>
              </article>
            ))}
          </div>
        </>
      )}
      {account?.kind !== 'registered' && (
        <p>
          <a href="/signin">Sign in or link your account</a> before buying so you can recover your
          purchases.
        </p>
      )}
      <p role="status">{message}</p>
      {purchases.status === 'ready' && purchases.data.items.length > 0 && (
        <>
          <h3>Your purchases</h3>
          <ul>
            {purchases.data.items.map((p) => (
              <li key={p.id}>
                {p.sku} — {p.status}{' '}
                <button disabled={busy} onClick={() => void reconcile(p.id)}>
                  Check payment
                </button>
              </li>
            ))}
          </ul>
        </>
      )}
    </section>
  );
}
