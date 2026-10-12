import { useEffect, useRef, type FormEvent, type ReactNode } from 'react';
import { GameArt, type ArtName } from '../art.tsx';
import { t, useLocale } from '../i18n.tsx';
import { Link } from '../router.tsx';
import type { Load } from '../state.tsx';
import { Empty, ErrorNotice, Loading } from './common.tsx';
import '../styles/library.css';

export function LibraryHeader({
  title,
  description,
  art,
  actions,
}: {
  title: string;
  description: ReactNode;
  art: ArtName;
  actions: ReactNode;
}) {
  return (
    <header className="library-header">
      <GameArt name={art} size={56} />
      <div className="library-heading">
        <h1>{title}</h1>
        <p>{description}</p>
      </div>
      <div className="library-actions">{actions}</div>
    </header>
  );
}

export function LibraryNav({ label, children }: { label: string; children: ReactNode }) {
  return (
    <nav className="seg library-nav" aria-label={label}>
      {children}
    </nav>
  );
}

export function LibraryField({
  label,
  search = false,
  children,
}: {
  label: string;
  search?: boolean;
  children: ReactNode;
}) {
  return (
    <label className={`library-field${search ? ' library-search' : ''}`}>
      <span>{label}</span>
      {children}
    </label>
  );
}

export function LibraryFilters({
  children,
  onSubmit,
  extra,
  activeExtra = 0,
}: {
  children: ReactNode;
  onSubmit: (event: FormEvent<HTMLFormElement>) => void;
  extra?: ReactNode;
  activeExtra?: number;
}) {
  useLocale();
  return (
    <form className="library-filters" role="search" onSubmit={onSubmit}>
      <button type="submit" hidden>
        {t('Search')}
      </button>
      <div className="library-filter-row">{children}</div>
      {extra && (
        <details className="library-more-filters">
          <summary>
            {t('More filters')}
            {activeExtra > 0 && <> ({activeExtra})</>}
          </summary>
          <div className="library-filter-row">{extra}</div>
        </details>
      )}
    </form>
  );
}

export function LibraryGrid({ children }: { children: ReactNode }) {
  return <div className="library-grid">{children}</div>;
}

/** Use a single link for passive cards, an article for cards with separate actions. */
export function LibraryCard({
  children,
  to,
  className = '',
  id,
  testId,
  skinId,
}: {
  children: ReactNode;
  to?: string;
  className?: string;
  id?: string;
  testId?: string;
  skinId?: string;
}) {
  const classes = `library-card ${className}`;
  return to ? (
    <Link to={to} className={classes} id={id} data-testid={testId} data-skin-id={skinId}>
      {children}
    </Link>
  ) : (
    <article className={classes} id={id} data-testid={testId} data-skin-id={skinId}>
      {children}
    </article>
  );
}

export function LibraryEmpty({
  children,
  art,
  action,
}: {
  children: ReactNode;
  art: ArtName;
  action?: ReactNode;
}) {
  return (
    <Empty art={art}>
      <div className="library-empty-copy">{children}</div>
      {action && <div className="library-actions">{action}</div>}
    </Empty>
  );
}

export function LibraryResults<T>({
  load,
  retry,
  children,
  busy = false,
  count,
}: {
  load: Load<T>;
  busy?: boolean;
  count?: (data: T) => number;
  retry: () => void;
  children: (data: T) => ReactNode;
}) {
  useLocale();
  const region = useRef<HTMLDivElement>(null);
  const focused = useRef<{ element: HTMLElement; cards: number } | null>(null);
  useEffect(() => {
    // A deliberate move away while loading cancels restoration.
    const moved = (event: Event) => {
      if (event.target instanceof Node && !region.current?.contains(event.target))
        focused.current = null;
    };
    document.addEventListener('focusin', moved);
    document.addEventListener('pointerdown', moved);
    return () => {
      document.removeEventListener('focusin', moved);
      document.removeEventListener('pointerdown', moved);
    };
  }, []);
  useEffect(() => {
    const previous = focused.current;
    if (!previous || previous.element.isConnected || document.activeElement !== document.body)
      return;
    if (previous.element.textContent?.trim() === t('Clear filters')) {
      region.current
        ?.closest('.library-page')
        ?.querySelector<HTMLInputElement>('input[type="search"]')
        ?.focus();
      focused.current = null;
    } else if (load.status !== 'loading' && !busy) {
      const cards = region.current?.querySelectorAll<HTMLElement>('.library-card');
      const card = cards?.[cards.length > previous.cards ? previous.cards : 0];
      const target = card?.matches('a, button')
        ? card
        : card?.querySelector<HTMLElement>('a, button:not(:disabled)');
      (target ?? region.current)?.focus();
      focused.current = null;
    }
  }, [load.status, busy]);
  const pending = busy || load.status === 'loading';
  return (
    <>
      <p className="library-status" role="status" aria-live="polite" aria-atomic="true">
        {pending
          ? t(' Loading…')
          : load.status === 'ready' && count
            ? t('Results: {value0}', { value0: count(load.data) })
            : ''}
      </p>
      <div
        ref={region}
        className="library-results"
        tabIndex={-1}
        aria-busy={pending}
        onFocusCapture={(event) => {
          focused.current = {
            element: event.target as HTMLElement,
            cards: region.current?.querySelectorAll('.library-card').length ?? 0,
          };
        }}
      >
        {load.status === 'loading' ? (
          <div className="library-feedback" aria-hidden="true">
            <Loading />
          </div>
        ) : load.status === 'error' ? (
          <div className="library-feedback">
            <ErrorNotice error={load.error} />
            <button onClick={retry}>{t('Try again')}</button>
          </div>
        ) : (
          <>
            {busy && (
              <div aria-hidden="true">
                <Loading />
              </div>
            )}
            {children(load.data)}
          </>
        )}
      </div>
    </>
  );
}

/** Controlled draft and committed query, so URL-driven libraries retain navigation state. */
export function useLibrarySearch(draft: string, query: string, apply: (query: string) => void) {
  const callback = useRef(apply);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  useEffect(() => {
    callback.current = apply;
  });
  useEffect(() => {
    if (draft.trim() === query) return;
    timer.current = setTimeout(() => callback.current(draft.trim()), 250);
    return () => clearTimeout(timer.current);
  }, [draft, query]);
  return () => {
    clearTimeout(timer.current);
    callback.current(draft.trim());
  };
}
