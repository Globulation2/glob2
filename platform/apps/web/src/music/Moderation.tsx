import { useState } from 'react';
import { request } from '../api.ts';
import { useLoad } from '../state.tsx';
import { Link } from '../router.tsx';
export function MusicReports() {
  const [notice, setNotice] = useState('');
  const health = useLoad(
    (signal) =>
      request<{ states: { status: string; count: number; oldest: string }[]; storedBytes: number }>(
        'GET',
        '/api/v1/admin/music-status',
        { signal },
      ),
    [],
  );
  const data = useLoad(
    (signal) =>
      request<{ items: { id: string; release_id: string; reason: string }[] }>(
        'GET',
        '/api/v1/admin/music-reports',
        { signal },
      ),
    [],
  );
  async function resolve(id: string, hidden: boolean) {
    try {
      await request('PUT', `/api/v1/admin/music/${id}`, {
        body: {
          hidden,
          reason: hidden
            ? 'Hidden following community report.'
            : 'Report reviewed; release remains available.',
        },
      });
      data.reload();
    } catch (error) {
      setNotice(String(error));
    }
  }
  return (
    <section>
      <h2>Music reports</h2>
      {health.status === 'ready' && (
        <p>
          {health.data.states.map((s) => `${s.status}: ${s.count}`).join(' · ')} ·{' '}
          {(health.data.storedBytes / 1048576).toFixed(1)} MiB stored
        </p>
      )}
      {notice && <p role="alert">{notice}</p>}
      {data.status === 'loading' ? (
        <p>Loading…</p>
      ) : data.status === 'error' ? (
        <p role="alert">{data.error.message}</p>
      ) : (
        data.data.items.map((report) => (
          <article key={report.id} className="card">
            <Link to={`/music/${report.release_id}`}>Open music release</Link>
            <p>{report.reason}</p>
            <button onClick={() => void resolve(report.release_id, true)}>
              Hide release and resolve
            </button>
            <button onClick={() => void resolve(report.release_id, false)}>
              Keep available and resolve
            </button>
          </article>
        ))
      )}
    </section>
  );
}
