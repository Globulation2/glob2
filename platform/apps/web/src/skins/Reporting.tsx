import { message as sourceMessage } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { skinAssetUrl } from './assetUrls.ts';
import { useState } from 'react';
import type { MatchColonySkin } from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad, useSession } from '../state.tsx';

function ReportSkin({ versionId }: { versionId: string }) {
  useLocale();
  const { account } = useSession();
  const [open, setOpen] = useState(false),
    [busy, setBusy] = useState(false);
  const [reason, setReason] = useState(''),
    [message, setMessage] = useState('');
  if (!account) return <a href="/signin">{t('Sign in to report this skin')}</a>;
  async function report() {
    setBusy(true);
    setMessage('');
    try {
      await request('POST', `/api/v1/skins/versions/${versionId}/reports`, { body: { reason } });
      setMessage(sourceMessage('Report received.'));
      setOpen(false);
    } catch (e) {
      setMessage(e instanceof Error ? e.message : t('Could not send report.'));
    } finally {
      setBusy(false);
    }
  }
  return (
    <>
      <button disabled={busy} onClick={() => setOpen(!open)}>
        {t('Report skin')}
      </button>
      {open && (
        <fieldset disabled={busy}>
          <label>
            {t('Report reason')}{' '}
            <textarea maxLength={1000} value={reason} onChange={(e) => setReason(e.target.value)} />
          </label>
          <button disabled={!reason.trim()} onClick={() => void report()}>
            {t('Send skin report')}
          </button>
        </fieldset>
      )}
      <p role="status">{message}</p>
    </>
  );
}
export function MatchSkinLooks({ matchId }: { matchId: string }) {
  useLocale();
  const { account } = useSession();
  const load = useLoad(
    (signal) =>
      request<{ colonySkins: MatchColonySkin[] }>('GET', `/api/v1/matches/${matchId}/skins`, {
        signal,
      }),
    [matchId],
  );
  if (load.status !== 'ready' || !load.data.colonySkins.length) return null;
  return (
    <section aria-label={t('Match colony skins')}>
      <h2>{t('Colony skins')}</h2>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 16 }}>
        {load.data.colonySkins.map((skin) => (
          <article
            key={skin.team}
            aria-label={t('Colony {value0} skin', { value0: skin.team + 1 })}
          >
            <h3>
              <RichMessage source={'Colony {slot0}'} slots={{ slot0: skin.team + 1 }} />
            </h3>
            <img
              width={128}
              height={128}
              src={skinAssetUrl(skin.version, 'texture')}
              alt={t('Colony {value0} paint', { value0: skin.team + 1 })}
            />
            <ReportSkin
              key={`${skin.version.id}:${account?.id ?? 'guest'}`}
              versionId={skin.version.id}
            />
          </article>
        ))}
      </div>
    </section>
  );
}
