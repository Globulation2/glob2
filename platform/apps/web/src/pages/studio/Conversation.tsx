import { useEffect, useRef, useState } from 'react';
import { ART } from '../../art.tsx';
import type { Thread } from './types.ts';
export function Conversation({
  thread,
  active,
  loadEarlier,
  busy,
  choose,
}: {
  thread?: Thread;
  active: boolean;
  loadEarlier: () => void;
  busy: boolean;
  choose: (v: string) => void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const bottom = useRef(true);
  const [unread, setUnread] = useState(false);
  const historyAnchor = useRef<
    { firstId: string | undefined; height: number; top: number } | undefined
  >(undefined);
  const firstMessageId = thread?.messages[0]?.id;
  useEffect(() => {
    const anchor = historyAnchor.current;
    if (anchor && ref.current && firstMessageId !== anchor.firstId) {
      ref.current.scrollTop = anchor.top + ref.current.scrollHeight - anchor.height;
      historyAnchor.current = undefined;
    } else if (bottom.current && ref.current) ref.current.scrollTop = ref.current.scrollHeight;
    else setUnread(true);
  }, [thread?.messages.length, firstMessageId, active]);
  return (
    <div className="ms-chat-container">
      <div
        className="ms-chat"
        tabIndex={0}
        ref={ref}
        role="log"
        aria-label="Map design conversation"
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
            Load earlier conversation and versions
          </button>
        )}
        {!thread?.messages.length && (
          <div className="ms-chat-intro">
            <img src={ART.swarm} alt="" />
            <h3>What will your world look like?</h3>
            <p>
              Dream up a landscape. We’ll work through the details together, then bring it to life.
            </p>
            <div className="ms-prompt-ideas">
              {[
                'A ring of islands around a shared lagoon',
                'Wooded hills with wide routes between colonies',
                'A winding river with room to grow',
              ].map((text, i) => (
                <button key={text} onClick={() => choose(text)}>
                  <span aria-hidden="true">{['◈', '♧', '≈'][i]}</span>
                  {text}
                  <span aria-hidden="true">↗</span>
                </button>
              ))}
            </div>
          </div>
        )}
        {thread?.messages.map((m) => (
          <article className={`ms-message ms-message-${m.role}`} key={m.id}>
            <div className="ms-message-author">
              <span aria-hidden="true">{m.role === 'user' ? '◉' : '✧'}</span>
              {m.role === 'user' ? 'You' : 'Map designer'}
              {m.created_at && (
                <time dateTime={m.created_at}>
                  {new Date(m.created_at).toLocaleTimeString([], {
                    hour: '2-digit',
                    minute: '2-digit',
                  })}
                </time>
              )}
            </div>
            <p>{m.text}</p>
          </article>
        ))}
        {active && (
          <article className="ms-message ms-message-assistant">
            <div className="ms-message-author">✧ Map designer</div>
            <div className="ms-typing" aria-label="The map designer is replying">
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
          New messages ↓
        </button>
      )}
    </div>
  );
}
