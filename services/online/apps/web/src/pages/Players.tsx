import { t, tp, useLocale } from '../i18n.tsx';
import { useEffect, useId, useRef, useState } from 'react';
import type { PlayerDirectory } from '@glob2/protocol';
import { api } from '../api.ts';
import { GameArt } from '../art.tsx';
import { AiMark, Avatar, ErrorNotice } from '../components/common.tsx';
import { Link, useRouter } from '../router.tsx';
import { playerHref } from '../playerLinks.ts';

function Highlight({ name, query }: { name: string; query: string }) {
  useLocale();
  const at = name.toLowerCase().indexOf(query.trim().toLowerCase());
  return at < 0 || !query.trim() ? (
    <>{name}</>
  ) : (
    <>
      {name.slice(0, at)}
      <mark>{name.slice(at, at + query.trim().length)}</mark>
      {name.slice(at + query.trim().length)}
    </>
  );
}
export function Players() {
  useLocale();
  const { navigate } = useRouter();
  const [q, setQ] = useState('');
  const [kind, setKind] = useState('all');
  const [cursors, setCursors] = useState<string[]>([]);
  const [data, setData] = useState<PlayerDirectory>();
  const [busy, setBusy] = useState(true);
  const [error, setError] = useState<Error>();
  const [active, setActive] = useState(-1);
  const [expanded, setExpanded] = useState(true);
  const [attempt, setAttempt] = useState(0);
  const scrollSelection = useRef(false);
  const listId = useId();
  const input = useRef<HTMLInputElement>(null);
  const cursor = cursors.at(-1);
  useEffect(() => {
    const controller = new AbortController();
    const timer = setTimeout(
      () => {
        void api.players({ q, participants: kind, cursor, limit: 24 }, controller.signal).then(
          (result) => {
            if (!controller.signal.aborted) {
              setData(result);
              setBusy(false);
            }
          },
          (e: Error) => {
            if (!controller.signal.aborted) {
              setError(e);
              setBusy(false);
            }
          },
        );
      },
      q ? 200 : 0,
    );
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [q, kind, cursor, attempt]);
  useEffect(() => {
    if (scrollSelection.current && active >= 0) {
      document.getElementById(`${listId}-${active}`)?.scrollIntoView({ block: 'nearest' });
    }
    scrollSelection.current = false;
  }, [active, listId]);
  const reset = () => {
    setData(undefined);
    setError(undefined);
    setBusy(true);
    setActive(-1);
    setExpanded(true);
  };
  return (
    <>
      <div className="page-head">
        <GameArt name="worker" size={72} className="head-art" />
        <div>
          <h1>{t('Players')}</h1>
          <p className="sub">
            {t(
              'Meet the colonies. Find a player, explore their games, or get to know your next AI opponent.',
            )}
          </p>
        </div>
      </div>
      <div className="directory-search card">
        <label htmlFor={`${listId}-search`}>{t('Find a player')}</label>
        <input
          ref={input}
          id={`${listId}-search`}
          className="player-search"
          type="search"
          placeholder={t('Search players and AI opponents…')}
          maxLength={64}
          autoComplete="off"
          role="combobox"
          aria-autocomplete="list"
          aria-expanded={expanded}
          aria-controls={listId}
          aria-activedescendant={active >= 0 ? `${listId}-${active}` : undefined}
          value={q}
          onFocus={() => setExpanded(true)}
          onChange={(e) => {
            setQ(e.target.value);
            setCursors([]);
            reset();
          }}
          onKeyDown={(e) => {
            const count = data?.items.length ?? 0;
            if (e.key === 'Escape') {
              e.preventDefault();
              setExpanded(false);
              setActive(-1);
            }
            if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
              e.preventDefault();
              setExpanded(true);
              scrollSelection.current = true;
              setActive(
                count
                  ? active < 0
                    ? e.key === 'ArrowDown'
                      ? 0
                      : count - 1
                    : (active + (e.key === 'ArrowDown' ? 1 : -1) + count) % count
                  : -1,
              );
            }
            if (e.key === 'Enter' && expanded && active >= 0 && data?.items[active]) {
              e.preventDefault();
              navigate(playerHref(data.items[active]));
            }
          }}
        />
        <div className="seg" role="group" aria-label={t('Player types')}>
          {[
            ['all', t('All')],
            ['humans', t('Humans')],
            ['ai', 'AI'],
          ].map(([value, label]) => (
            <button
              key={value}
              className={kind === value ? 'on' : ''}
              aria-pressed={kind === value}
              onClick={() => {
                if (value === kind) return;
                setKind(value ?? 'all');
                setCursors([]);
                reset();
              }}
            >
              {label}
            </button>
          ))}
        </div>
      </div>
      <p className="caption" role="status">
        {busy
          ? t('Finding players…')
          : error
            ? t('Search unavailable')
            : data?.items.length
              ? tp('{count} player on this page', '{count} players on this page', data.items.length)
              : t('No players found. Try another name.')}
      </p>
      {error && (
        <>
          <ErrorNotice error={error} />
          <button
            onClick={() => {
              reset();
              setAttempt((n) => n + 1);
            }}
          >
            {t('Try again')}
          </button>
        </>
      )}
      <div
        id={listId}
        role="listbox"
        aria-label={t('Players')}
        aria-busy={busy}
        className="player-grid"
        hidden={!expanded}
      >
        {data?.items.map((player, i) => {
          const name = player.kind === 'account' ? player.account.displayName : player.displayName;
          return (
            <Link
              key={playerHref(player)}
              id={`${listId}-${i}`}
              role="option"
              aria-selected={active === i}
              className={`player-card card ${active === i ? 'selected' : ''}`}
              to={playerHref(player)}
              onMouseEnter={() => setActive(i)}
            >
              {player.kind === 'account' ? (
                <Avatar account={player.account} size="large" />
              ) : (
                <AiMark />
              )}
              <div className="grow">
                <strong>
                  <Highlight name={name} query={q} />
                </strong>
                <div className="caption">
                  {player.kind === 'account'
                    ? t('View profile and games')
                    : t('Explore ratings and games')}
                </div>
              </div>
              {player.kind === 'ai' && <span className="badge">{t('AI')}</span>}
              <span aria-hidden="true">{t('→')}</span>
            </Link>
          );
        })}
      </div>
      <div className="pager">
        {cursors.length > 0 && (
          <button
            onClick={() => {
              setCursors(cursors.slice(0, -1));
              reset();
              input.current?.focus();
            }}
          >
            {t('Previous')}
          </button>
        )}
        {data?.nextCursor && (
          <button
            onClick={() => {
              setCursors([...cursors, data.nextCursor ?? '']);
              reset();
              input.current?.focus();
            }}
          >
            {t('Next')}
          </button>
        )}
      </div>
    </>
  );
}
