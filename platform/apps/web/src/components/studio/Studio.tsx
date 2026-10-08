import { requestState } from './adapters.ts';
import {
  useEffect,
  useId,
  useRef,
  useState,
  type CSSProperties,
  type ReactNode,
  type RefObject,
  type TextareaHTMLAttributes,
} from 'react';
import { Icon, type IconName } from '../../icons.tsx';
import { useSession } from '../../state.tsx';
import { studioLocal } from './storage.ts';
import './studio.css';

export function StudioShell({
  children,
  className = '',
}: {
  children: ReactNode;
  className?: string;
}) {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const viewport = window.visualViewport;
    const update = () =>
      ref.current?.style.setProperty(
        '--studio-height',
        `${viewport?.height ?? window.innerHeight}px`,
      );
    update();
    viewport?.addEventListener('resize', update);
    window.addEventListener('resize', update);
    return () => {
      viewport?.removeEventListener('resize', update);
      window.removeEventListener('resize', update);
    };
  }, []);
  return (
    <div ref={ref} className={`studio-page ${className}`}>
      {children}
    </div>
  );
}
export function StudioHeader({
  title,
  icon,
  children,
}: {
  title: string;
  icon: IconName;
  children?: ReactNode;
}) {
  return (
    <header className="studio-header">
      <Icon name={icon} />
      <h1>{title}</h1>
      <div className="studio-header-actions">{children}</div>
    </header>
  );
}
export function StudioTabs({
  label,
  value,
  onChange,
  items,
  panels,
}: {
  label: string;
  value: string;
  onChange: (value: string) => void;
  items: { id: string; label: string; icon?: IconName }[];
  panels?: Record<string, string>;
}) {
  const ref = useRef<HTMLDivElement>(null);
  return (
    <div className="studio-tabs" role="tablist" aria-label={label} ref={ref}>
      {items.map((item, index) => (
        <button
          key={item.id}
          type="button"
          role="tab"
          id={`studio-tab-${label.toLowerCase().replaceAll(' ', '-')}-${item.id}`}
          aria-controls={
            panels?.[item.id] ??
            `studio-panel-${label.toLowerCase().replaceAll(' ', '-')}-${item.id}`
          }
          aria-selected={value === item.id}
          tabIndex={value === item.id ? 0 : -1}
          onClick={() => onChange(item.id)}
          onKeyDown={(e) => {
            const direction = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
            if (direction || e.key === 'Home' || e.key === 'End') {
              e.preventDefault();
              const next =
                e.key === 'Home'
                  ? 0
                  : e.key === 'End'
                    ? items.length - 1
                    : (index + direction + items.length) % items.length;
              ref.current?.querySelectorAll<HTMLButtonElement>('button')[next]?.focus();
            }
          }}
        >
          {item.icon && <Icon name={item.icon} size={18} />} {item.label}
        </button>
      ))}
    </div>
  );
}
export function StudioWorkspace({
  conversation,
  artifact,
  attention,
  focusChat,
}: {
  conversation: ReactNode;
  artifact: ReactNode;
  attention?: string;
  focusChat?: number;
}) {
  const { account } = useSession();
  const key = `studio-split:${account?.id ?? 'anonymous'}`;
  const [split, setSplit] = useState(() => {
    const n = Number(studioLocal.getItem(key) ?? 40);
    return Number.isFinite(n) ? Math.max(25, Math.min(65, n)) : 40;
  });
  const [pane, setPane] = useState('chat');
  const [width, setWidth] = useState(() => window.innerWidth);
  const ref = useRef<HTMLDivElement>(null);
  const id = useId();
  useEffect(() => {
    const element = ref.current;
    if (!element) return;
    const resize = () => setWidth(element.clientWidth || window.innerWidth);
    resize();
    if (typeof ResizeObserver === 'undefined') return;
    const observer = new ResizeObserver(resize);
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    if (focusChat) {
      requestAnimationFrame(() => {
        setPane('chat');
        requestAnimationFrame(() =>
          ref.current?.querySelector<HTMLTextAreaElement>('textarea')?.focus(),
        );
      });
    }
  }, [focusChat]);
  const narrow = width < 760;
  const minimum = width ? Math.max(25, (320 / width) * 100) : 25;
  const maximum = width ? Math.min(65, ((width - 368) / width) * 100) : 65;
  const actual = narrow ? split : Math.max(minimum, Math.min(maximum, split));
  const resize = (value: number) => {
    const next = Math.max(minimum, Math.min(maximum, value));
    setSplit(next);
    studioLocal.setItem(key, String(next));
  };
  return (
    <div
      className="studio-workspace"
      ref={ref}
      data-narrow={narrow}
      data-pane={pane}
      style={{ '--studio-split': `${actual}%` } as CSSProperties}
    >
      {narrow && (
        <StudioTabs
          label="Studio view"
          panels={{ chat: `${id}-chat`, preview: `${id}-artifact` }}
          value={pane}
          onChange={setPane}
          items={[
            { id: 'chat', label: 'Chat', icon: 'message' },
            { id: 'preview', label: attention ? `Preview · ${attention}` : 'Preview', icon: 'eye' },
          ]}
        />
      )}
      <section
        id={`${id}-chat`}
        className="studio-conversation"
        role={narrow ? 'tabpanel' : undefined}
        aria-label="Conversation"
        hidden={narrow && pane !== 'chat'}
      >
        {conversation}
      </section>
      {!narrow && (
        <div
          className="studio-separator"
          role="separator"
          tabIndex={0}
          aria-label="Resize conversation"
          aria-controls={`${id}-chat ${id}-artifact`}
          aria-orientation="vertical"
          aria-valuemin={Math.round(minimum)}
          aria-valuemax={Math.round(maximum)}
          aria-valuenow={Math.round(actual)}
          onKeyDown={(e) => {
            if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) {
              e.preventDefault();
              resize(
                e.key === 'Home'
                  ? minimum
                  : e.key === 'End'
                    ? maximum
                    : actual + (e.key === 'ArrowLeft' ? -2 : 2),
              );
            }
          }}
          onPointerDown={(e) => e.currentTarget.setPointerCapture(e.pointerId)}
          onPointerMove={(e) => {
            if (e.currentTarget.hasPointerCapture(e.pointerId) && ref.current)
              resize(((e.clientX - ref.current.getBoundingClientRect().left) / width) * 100);
          }}
          onPointerUp={(e) => e.currentTarget.releasePointerCapture(e.pointerId)}
        />
      )}
      <section
        id={`${id}-artifact`}
        className="studio-artifact"
        role={narrow ? 'tabpanel' : undefined}
        aria-label="Creation workspace"
        hidden={narrow && pane !== 'preview'}
      >
        {!narrow && (
          <details className="studio-width">
            <summary>
              <Icon name="adjustments-horizontal" size={18} /> Pane widths
            </summary>
            <div>
              {[35, 40, 50].map((value) => (
                <button key={value} onClick={() => resize(value)}>
                  {value === 40 ? 'Reset · ' : ''}
                  {value}% chat
                </button>
              ))}
            </div>
          </details>
        )}
        {artifact}
      </section>
    </div>
  );
}
export function ConversationPane({
  children,
  label,
  count,
  firstMessageId,
}: {
  children: ReactNode;
  label: string;
  count: number;
  firstMessageId?: string;
}) {
  const ref = useRef<HTMLDivElement>(null),
    following = useRef(true),
    previous = useRef<{ height: number; top: number; first: string }>({
      height: 0,
      top: 0,
      first: '',
    });
  const [unread, setUnread] = useState(false);
  useEffect(() => {
    const box = ref.current;
    if (!box) return;
    const first = firstMessageId ?? box.firstElementChild?.textContent ?? '';
    if (previous.current.first && first !== previous.current.first)
      box.scrollTop = previous.current.top + box.scrollHeight - previous.current.height;
    else if (following.current) box.scrollTop = box.scrollHeight;
    else setUnread(true);
    previous.current = { first, height: box.scrollHeight, top: box.scrollTop };
  }, [count, firstMessageId]);
  return (
    <div className="studio-log-wrap">
      <div
        className="studio-log"
        ref={ref}
        role="log"
        tabIndex={0}
        aria-label={label}
        aria-live="polite"
        aria-relevant="additions"
        onScroll={(e) => {
          const box = e.currentTarget;
          following.current = box.scrollHeight - box.scrollTop - box.clientHeight < 70;
          previous.current = { ...previous.current, height: box.scrollHeight, top: box.scrollTop };
          if (following.current) setUnread(false);
        }}
      >
        {children}
      </div>
      {unread && (
        <button
          className="studio-unread"
          onClick={() => {
            const box = ref.current;
            if (box) box.scrollTop = box.scrollHeight;
            following.current = true;
            setUnread(false);
          }}
        >
          New messages <Icon name="chevron-down" size={18} />
        </button>
      )}
    </div>
  );
}
export function ChatInput({
  inputRef,
  onSend,
  onKeyDown,
  onChange,
  ...props
}: TextareaHTMLAttributes<HTMLTextAreaElement> & {
  inputRef?: RefObject<HTMLTextAreaElement | null>;
  onSend: () => void;
}) {
  return (
    <textarea
      {...props}
      ref={inputRef}
      rows={2}
      onChange={(e) => {
        onChange?.(e);
        e.currentTarget.style.height = 'auto';
        e.currentTarget.style.height = `${Math.min(150, e.currentTarget.scrollHeight)}px`;
      }}
      onKeyDown={(e) => {
        onKeyDown?.(e);
        if (
          !e.defaultPrevented &&
          e.key === 'Enter' &&
          !e.shiftKey &&
          !e.nativeEvent.isComposing &&
          e.nativeEvent.keyCode !== 229
        ) {
          e.preventDefault();
          onSend();
        }
      }}
    />
  );
}
export function ChatComposer({
  value,
  onChange,
  onSend,
  label,
  placeholder,
  disabledReason,
  pricing,
  target,
  tools,
  inputRef,
  maxLength = 8000,
}: {
  value: string;
  onChange: (value: string) => void;
  onSend: () => void;
  label: string;
  placeholder?: string;
  disabledReason?: string;
  pricing: string;
  target?: string;
  tools?: ReactNode;
  inputRef?: RefObject<HTMLTextAreaElement | null>;
  maxLength?: number;
}) {
  const id = useId();
  const send = () => {
    if (value.trim() && !disabledReason) onSend();
  };
  return (
    <form
      className="studio-composer"
      onSubmit={(e) => {
        e.preventDefault();
        send();
      }}
    >
      {target && (
        <p className="studio-edit-target">
          <Icon name="pencil" size={16} />
          {target}
        </p>
      )}
      {tools && (
        <details className="studio-composer-tools">
          <summary>
            <Icon name="settings" size={18} /> Settings & references
          </summary>
          <div>{tools}</div>
        </details>
      )}
      <label htmlFor={id}>{label}</label>
      <ChatInput
        id={id}
        inputRef={inputRef}
        value={value}
        maxLength={maxLength}
        placeholder={placeholder}
        onChange={(e) => onChange(e.target.value)}
        onSend={send}
        aria-describedby={`${id}-help`}
      />
      <div className="studio-compose-actions">
        <span>Enter to send · Shift + Enter for a new line</span>
        <button
          className="primary"
          aria-disabled={!!disabledReason || !value.trim()}
          onClick={(e) => {
            if (disabledReason || !value.trim()) e.preventDefault();
          }}
        >
          <Icon name="send" size={18} /> Send
        </button>
      </div>
      <p id={`${id}-help`} className="studio-compose-help">
        {pricing}
        {disabledReason && ` ${disabledReason}`}
      </p>
    </form>
  );
}
export function ArtifactPane({ children, title }: { children: ReactNode; title: string }) {
  return (
    <div className="studio-artifact-content">
      <h2>{title}</h2>
      {children}
    </div>
  );
}
export function RevisionControls({
  children,
  viewed,
  target,
  follow,
}: {
  children?: ReactNode;
  viewed: string;
  target?: string;
  follow?: () => void;
}) {
  return (
    <div className="studio-revisions">
      <strong>Viewing {viewed}</strong>
      {target && <span>Editing {target}</span>}
      {follow && (
        <button onClick={follow}>
          <Icon name="refresh" size={18} /> Follow latest
        </button>
      )}
      {children}
    </div>
  );
}
export function RequestStatus({ status, children }: { status: string; children?: ReactNode }) {
  status = requestState(status);
  const icon: IconName =
    status === 'failed'
      ? 'alert-triangle'
      : status === 'uncertain'
        ? 'wifi-off'
        : status === 'completed'
          ? 'check'
          : 'loader-2';
  return (
    <div className="studio-request-status" role="status">
      <Icon name={icon} size={18} />
      <span>{children ?? status}</span>
    </div>
  );
}
export function ValidationSummary({ children, version }: { children: ReactNode; version: string }) {
  return (
    <section className="studio-validation" aria-label={`Checks for ${version}`}>
      <h3>Checks · {version}</h3>
      {children}
    </section>
  );
}
export function ReleaseDialog({
  open,
  onClose,
  title,
  children,
}: {
  open: boolean;
  onClose: () => void;
  title: string;
  children: ReactNode;
}) {
  const ref = useRef<HTMLDialogElement>(null),
    trigger = useRef<HTMLElement | null>(null);
  const id = useId();
  useEffect(() => {
    const dialog = ref.current;
    if (open && !dialog?.open) {
      trigger.current = document.activeElement as HTMLElement;
      dialog?.showModal();
    } else if (!open && dialog?.open) dialog.close();
  }, [open]);
  return (
    <dialog
      className="studio-dialog"
      ref={ref}
      aria-labelledby={id}
      onCancel={onClose}
      onClose={() => {
        onClose();
        trigger.current?.focus();
      }}
    >
      <header>
        <h2 id={id}>{title}</h2>
        <button
          type="button"
          onClick={onClose}
          aria-label="Close panel"
          aria-describedby={`${id}-close-help`}
        >
          <Icon name="x" />
          <span id={`${id}-close-help`} className="studio-tooltip" role="tooltip">
            Close panel · Escape
          </span>
        </button>
      </header>
      {open && children}
    </dialog>
  );
}
export function CreditPanel({
  domain,
  available,
  reserved,
  children,
}: {
  domain: string;
  available?: number;
  reserved?: number;
  children?: ReactNode;
}) {
  return (
    <section aria-label={`${domain} credits`}>
      <h3>
        {available ?? '…'} {domain} credits available
      </h3>
      <p>{reserved ?? 0} reserved. Credits belong to this studio.</p>
      {children}
    </section>
  );
}
export function NewStudio({
  value,
  onChange,
  onSend,
  disabledReason,
  pricing,
  projects,
  tools,
  title,
}: {
  value: string;
  onChange: (value: string) => void;
  onSend: () => void;
  disabledReason?: string;
  pricing: string;
  projects: ReactNode;
  tools?: ReactNode;
  title: string;
}) {
  return (
    <StudioWorkspace
      conversation={
        <>
          <ConversationPane label={`${title} conversation`} count={0}>
            <h2>What would you like to create?</h2>
            <p>
              Describe your idea, request a creation, or ask a question. Your project and its
              history stay private until you publish.
            </p>
            {projects}
          </ConversationPane>
          <ChatComposer
            value={value}
            onChange={onChange}
            onSend={onSend}
            label="Your first idea"
            disabledReason={disabledReason}
            pricing={pricing}
            tools={tools}
          />
        </>
      }
      artifact={
        <ArtifactPane title={title}>
          <p>
            Your creation will appear here. You can inspect, edit, compare, and publish saved
            versions.
          </p>
        </ArtifactPane>
      }
    />
  );
}
