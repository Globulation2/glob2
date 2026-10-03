import { useState } from 'react';
import type { MatchColonySkin } from '@glob2/protocol';
import { request } from '../api.ts';
import { useLoad, useSession } from '../state.tsx';

function ReportSkin({ versionId }: { versionId: string }) {
  const { account } = useSession();
  const [open, setOpen] = useState(false),
    [busy, setBusy] = useState(false);
  const [reason, setReason] = useState(''),
    [message, setMessage] = useState('');
  if (!account) return <a href="/signin">Sign in to report this skin</a>;
  async function report() {
    setBusy(true);
    setMessage('');
    try {
      await request('POST', `/api/v1/skins/versions/${versionId}/reports`, { body: { reason } });
      setMessage('Report received.');
      setOpen(false);
    } catch (e) {
      setMessage(e instanceof Error ? e.message : 'Could not send report.');
    } finally {
      setBusy(false);
    }
  }
  return (
    <>
      <button disabled={busy} onClick={() => setOpen(!open)}>
        Report skin
      </button>
      {open && (
        <fieldset disabled={busy}>
          <label>
            Report reason{' '}
            <textarea maxLength={1000} value={reason} onChange={(e) => setReason(e.target.value)} />
          </label>
          <button disabled={!reason.trim()} onClick={() => void report()}>
            Send skin report
          </button>
        </fieldset>
      )}
      <p role="status">{message}</p>
    </>
  );
}
export function MatchSkinLooks({ matchId }: { matchId: string }) {
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
    <section aria-label="Match colony skins">
      <h2>Colony skins</h2>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 16 }}>
        {load.data.colonySkins.map((skin) => (
          <article key={skin.team} aria-label={`Colony ${skin.team + 1} skin`}>
            <h3>Colony {skin.team + 1}</h3>
            <img
              width={128}
              height={128}
              src={`/api/v1/skins/versions/${skin.version.id}/texture`}
              alt={`Colony ${skin.team + 1} paint`}
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
