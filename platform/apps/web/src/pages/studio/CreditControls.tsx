import { statusLabel } from '../../i18n.tsx';
import { getLocale } from '../../i18n.tsx';
import { t, useLocale, RichMessage } from '../../i18n.tsx';
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
  useLocale();
  return (
    <section className="ms-credit-panel" aria-label={t('Manage credits')}>
      <div>
        <h2>{t('Your next great map')}</h2>
        <p>
          <RichMessage
            source={
              '{slot0} available · {slot1} reserved. Discussion requires an available credit; each delivered map or revision costs 1 credit. Failed generations return the credit.'
            }
            slots={{ slot0: wallet?.available ?? 0, slot1: wallet?.reserved ?? 0 }}
          />
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
            <RichMessage
              source={'Buy {slot0} credits · {slot1}'}
              slots={{ slot0: p.credits, slot1: price(p) }}
              singular={'Buy {slot0} credit · {slot1}'}
              count={Number(p.credits)}
            />
          </button>
        ))}
        {!wallet?.packs.length && <p>{t('Credit purchases are currently unavailable.')}</p>}
        <details>
          <summary>{t('Credit activity')}</summary>
          {wallet?.usage.map((entry) => (
            <p key={entry.id}>
              <RichMessage
                source={'{slot0} · {slot1} · {slot2}'}
                slots={{
                  slot0: new Date(entry.created_at).toLocaleString(getLocale()),
                  slot1: statusLabel(entry.kind),
                  slot2: entry.amount,
                }}
              />
            </p>
          ))}
        </details>
      </div>
      <button aria-label={t('Close credits')} onClick={close}>
        {t('×')}
      </button>
    </section>
  );
}
