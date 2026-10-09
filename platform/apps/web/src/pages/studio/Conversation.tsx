import { getLocale } from '../../i18n.tsx';
import { t, useLocale, RichMessage } from '../../i18n.tsx';
import { Icon } from '../../icons.tsx';
import { useEffect, useRef, useState } from 'react';
import { ART } from '../../art.tsx';
import type { Thread } from './types.ts';
import type { StudioProgress } from '@glob2/protocol';
export function Conversation({
  thread,
  active,
  loadEarlier,
  busy,
  choose,
  inspect,
  progress,
}: {
  thread?: Thread;
  active: boolean;
  loadEarlier: () => void;
  busy: boolean;
  choose: (v: string) => void;
  inspect: (id: string) => void;
  progress?: StudioProgress;
}) {
  useLocale();
  const ref = useRef<HTMLDivElement>(null);
  const bottom = useRef(true);
  const [unread, setUnread] = useState(false);
  const historyAnchor = useRef<
    { firstId: string | undefined; height: number; top: number } | undefined
  >(undefined);
  const builds =
    thread?.requests.filter(
      (r) => r.kind === 'generate' || ['failed', 'uncertain'].includes(r.status),
    ) ?? [];
  const entries = [
    ...(thread?.messages.map((message) => ({
      type: 'message' as const,
      message,
      created: message.created_at ?? '',
      id: message.id,
    })) ?? []),
    ...builds.map((build) => ({
      type: 'build' as const,
      build,
      created: build.created_at ?? '',
      id: build.id,
    })),
  ].sort(
    (a, b) =>
      a.created.localeCompare(b.created) ||
      (a.type === b.type ? a.id.localeCompare(b.id) : a.type === 'message' ? -1 : 1),
  );
  const firstMessageId = thread?.messages[0]?.id;
  useEffect(() => {
    const anchor = historyAnchor.current;
    if (anchor && ref.current && firstMessageId !== anchor.firstId) {
      ref.current.scrollTop = anchor.top + ref.current.scrollHeight - anchor.height;
      historyAnchor.current = undefined;
    } else if (bottom.current && ref.current) ref.current.scrollTop = ref.current.scrollHeight;
    else setUnread(true);
  }, [thread?.messages.length, thread?.requests, firstMessageId, active]);
  return (
    <div className="ms-chat-container">
      <div
        className="ms-chat"
        tabIndex={0}
        ref={ref}
        role="log"
        aria-label={t('Map design conversation')}
        aria-live="polite"
        onScroll={(e) => {
          const el = e.currentTarget;
          bottom.current = el.scrollHeight - el.scrollTop - el.clientHeight < 70;
          if (bottom.current) setUnread(false);
        }}
      >
        {(thread?.history?.messagesBefore || thread?.history?.requestsBefore) && (
          <button
            disabled={busy}
            onClick={() => {
              if (ref.current)
                historyAnchor.current = {
                  firstId: thread?.messages[0]?.id,
                  height: ref.current.scrollHeight,
                  top: ref.current.scrollTop,
                };
              loadEarlier();
            }}
          >
            {t('Load earlier conversation and versions')}
          </button>
        )}
        {!thread?.messages.length && (
          <div className="ms-chat-intro">
            <img src={ART.swarm} alt="" />
            <h2>{t('What will your world look like?')}</h2>
            <p>{t('Describe a map to build, ask a question, or explore an idea together.')}</p>
            <div className="ms-prompt-ideas">
              {[
                t('Create a ring of islands around a shared lagoon'),
                t('Build wooded hills with wide routes between colonies'),
                t('Create a winding river with room to grow'),
              ].map((text, i) => (
                <button key={text} onClick={() => choose(text)}>
                  <span aria-hidden="true">{['◈', '♧', '≈'][i]}</span>
                  {text}
                  <span aria-hidden="true">{t('↗')}</span>
                </button>
              ))}
            </div>
          </div>
        )}
        {entries.map((entry) => {
          if (entry.type === 'build') {
            const r = entry.build;
            if (r.kind === 'chat')
              return (
                <article
                  key={r.id}
                  className="ms-build-card"
                  aria-label={t('Designer request status')}
                >
                  <strong>
                    {r.status === 'uncertain'
                      ? t('Designer reply awaiting reconciliation')
                      : t('Designer request failed')}
                  </strong>
                  <p>{r.error ?? t('Your message could not be completed.')}</p>
                  {r.status === 'uncertain' ? (
                    <p>{t('No map build has started. Waiting for the provider outcome.')}</p>
                  ) : (
                    <p>{t('You can send a new message to try again.')}</p>
                  )}
                </article>
              );
            const checking =
              r.id === progress?.requestId &&
              progress.stages.some((s) => s.id === 'checks' && s.status === 'running');
            const label =
              r.status === 'ready'
                ? t('Delivered')
                : r.status === 'failed'
                  ? t('Failed')
                  : r.status === 'uncertain'
                    ? t('Awaiting provider outcome')
                    : r.status === 'queued'
                      ? t('Queued')
                      : checking || r.status === 'importing'
                        ? t('Checking')
                        : t('Building');
            return (
              <article
                key={r.id}
                className="ms-build-card"
                aria-label={t('Map build: {value0}', { value0: label })}
              >
                <strong>{label}</strong>
                <span>
                  {r.input.settings
                    ? t('{value0} × {value1} · {value2} players', {
                        value0: r.input.settings.width,
                        value1: r.input.settings.height,
                        value2: r.input.settings.players,
                      })
                    : t('Map build')}
                </span>
                <p>
                  {r.status === 'failed'
                    ? t('{value0} Your credit was returned.', {
                        value0: r.error ?? 'The build did not complete.',
                      })
                    : r.status === 'uncertain'
                      ? t('Your credit remains reserved while the provider outcome is reconciled.')
                      : r.status === 'ready'
                        ? t('Your map is ready to play.')
                        : t('Follow progress on the canvas.')}
                </p>
                <button onClick={() => inspect(r.id)}>
                  <RichMessage
                    source={'View {slot0}'}
                    slots={{ slot0: r.status === 'ready' ? t('map') : t('build') }}
                  />
                </button>
              </article>
            );
          }
          const m = entry.message;
          return (
            <article className={`ms-message ms-message-${m.role}`} key={m.id}>
              <div className="ms-message-author">
                <span aria-hidden="true">{m.role === 'user' ? '◉' : '✧'}</span>
                {m.role === 'user' ? t('You') : t('Map designer')}
                {m.created_at && (
                  <time dateTime={m.created_at}>
                    {new Date(m.created_at).toLocaleTimeString(getLocale(), {
                      hour: '2-digit',
                      minute: '2-digit',
                    })}
                  </time>
                )}
              </div>
              <p>{m.text}</p>
            </article>
          );
        })}
        {active && (
          <article className="ms-message ms-message-assistant">
            <div className="ms-message-author">{t('✧ Map designer')}</div>
            <div className="ms-typing" aria-label={t('The map designer is replying')}>
              <span />
              <span />
              <span />
            </div>
          </article>
        )}
      </div>
      {unread && (
        <button
          className="ms-new-messages"
          onClick={() => {
            if (ref.current) ref.current.scrollTop = ref.current.scrollHeight;
            bottom.current = true;
            setUnread(false);
          }}
        >
          {t('New messages ')}
          <Icon name="chevron-down" size={18} />
        </button>
      )}
    </div>
  );
}
