import { displayMessage, statusLabel } from '../i18n.tsx';
import { translateError } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { useState } from 'react';
import { request } from '../api.ts';
import { useLoad } from '../state.tsx';
import { Link } from '../router.tsx';
export function MusicReports() {
  useLocale();
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
            ? t('Hidden following community report.')
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
      <h2>{t('Music reports')}</h2>
      {health.status === 'ready' && (
        <p>
          <RichMessage
            source={'{slot0} · {slot1} MiB stored'}
            slots={{
              slot0: health.data.states
                .map((s) => `${statusLabel(s.status)}: ${s.count}`)
                .join(' · '),
              slot1: (health.data.storedBytes / 1048576).toFixed(1),
            }}
          />
        </p>
      )}
      {notice && <p role="alert">{displayMessage(notice)}</p>}
      {data.status === 'loading' ? (
        <p>{t('Loading…')}</p>
      ) : data.status === 'error' ? (
        <p role="alert">{translateError(data.error)}</p>
      ) : (
        data.data.items.map((report) => (
          <article key={report.id} className="card">
            <Link to={`/music/${report.release_id}`}>{t('Open music release')}</Link>
            <p>{report.reason}</p>
            <button onClick={() => void resolve(report.release_id, true)}>
              {t('Hide release and resolve')}
            </button>
            <button onClick={() => void resolve(report.release_id, false)}>
              {t('Keep available and resolve')}
            </button>
          </article>
        ))
      )}
    </section>
  );
}
