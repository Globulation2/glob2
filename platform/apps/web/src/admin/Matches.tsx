import { useState } from 'react';
import { request, api } from '../api.ts';
import { Loaded, MatchListView } from '../components/common.tsx';
import { useLoad, useSession } from '../state.tsx';
import { PageControls, useAdminFilters, useFilterDraft } from './filters.tsx';

export function Matches() {
  const { values, set } = useAdminFilters();
  const [q, setQ] = useFilterDraft(values['q'] ?? '');
  const load = useLoad((signal) => api.adminMatches(values, signal), [JSON.stringify(values)]);
  const { account } = useSession();
  return (
    <>
      <form
        className="toolbar"
        role="search"
        onSubmit={(e) => {
          e.preventDefault();
          set({ q: q.trim() });
        }}
      >
        <input
          aria-label="Search matches"
          placeholder="Match id, account id, player name or relay"
          value={q}
          onChange={(e) => setQ(e.target.value)}
          style={{ flex: '1 1 260px' }}
        />
        <select
          aria-label="Status"
          value={values['status'] ?? ''}
          onChange={(e) => set({ status: e.target.value })}
        >
          <option value="">Any status</option>
          <option value="starting">Starting</option>
          <option value="running">Running</option>
          <option value="ended">Ended</option>
          <option value="cancelled">Cancelled</option>
        </select>
        <label>
          Verification{' '}
          <select
            value={values['verification'] ?? ''}
            onChange={(e) => set({ verification: e.target.value })}
          >
            <option value="">Any</option>
            {['pending', 'failed', 'verified', 'diverged', 'unverifiable', 'not_applicable'].map(
              (v) => (
                <option key={v}>{v}</option>
              ),
            )}
          </select>
        </label>
        <button type="submit">Search</button>
      </form>
      <Loaded load={load}>
        {(page) => (
          <>
            <MatchListView matches={page.items} empty="No matches found." />
            {account?.role === 'admin' &&
              page.items
                .filter((m) => m.status === 'ended')
                .map((m) => <Reverify key={m.id} id={m.id} reload={load.reload} />)}
            <PageControls nextCursor={page.nextCursor} />
          </>
        )}
      </Loaded>
    </>
  );
}

function Reverify({ id, reload }: { id: string; reload: () => void }) {
  const [reason, setReason] = useState(''),
    [force, setForce] = useState(false),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('');
  async function act() {
    if (force && !window.confirm(`Force re-verification of ${id}?`)) return;
    if (busy) return;
    setBusy(true);
    setError('');
    try {
      await request('POST', `/api/v1/admin/matches/${id}/reverify`, { body: { force, reason } });
      reload();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  }
  return (
    <details>
      <summary>Re-verify {id}</summary>
      <p>
        Queues the stored match record for verification again. Force permits replacement of an
        existing verification result; confirm it separately.
      </p>
      <label>
        Reason <input value={reason} onChange={(e) => setReason(e.target.value)} maxLength={2000} />
      </label>
      <label>
        <input type="checkbox" checked={force} onChange={(e) => setForce(e.target.checked)} />
        Force re-verification
      </label>
      <button disabled={busy || !reason.trim()} onClick={() => void act()}>
        Re-verify
      </button>
      {error && <p role="alert">{error}</p>}
    </details>
  );
}
