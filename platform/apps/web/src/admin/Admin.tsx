import { statusLabel } from '../i18n.tsx';
import { t, useLocale, RichMessage } from '../i18n.tsx';
import { MusicReports } from '../music/Moderation.tsx';
import { SkinReports } from '../skins/Moderation.tsx';
import { Operations } from './Operations.tsx';
import { Overview } from './Overview.tsx';
import { Finances } from './Finances.tsx';
import { UnifiedReports, Content, Audit } from './Moderation.tsx';
import { Accounts } from './Accounts.tsx';
import { Matches } from './Matches.tsx';
// Central administration; legacy music and skin report URLs remain supported.
import { GameArt } from '../art.tsx';
import { Link } from '../router.tsx';
import { isModerator, useSession } from '../state.tsx';

const TABS = [
  { id: 'overview', name: 'Overview' },
  { id: 'finances', name: 'Finances' },
  { id: 'accounts', name: 'Accounts' },
  { id: 'matches', name: 'Matches' },
  { id: 'reports', name: 'Reports' },
  { id: 'content', name: 'Content' },
  { id: 'audit', name: 'Audit' },
  { id: 'operations', name: 'Operations' },
];

export function Admin({ tab }: { tab: string | undefined }) {
  useLocale();
  const { account } = useSession();
  if (account === undefined) return null;
  if (!account) {
    return (
      <div className="notice">
        <RichMessage
          source={'{slot0} with a moderator account.'}
          slots={{ slot0: <a href="/signin">{t('Sign in')}</a> }}
        />
      </div>
    );
  }
  if (!isModerator(account)) {
    return <div className="notice error">{t('This page is for moderators.')}</div>;
  }
  const current =
    (['music', 'skins'].includes(tab ?? '') ? tab : TABS.find((t) => t.id === tab)?.id) ??
    (account.role === 'admin' ? 'overview' : 'reports');
  return (
    <div className="admin-console">
      <div className="page-head">
        <GameArt name="clearingFlag" size={72} className="head-art" />
        <div className="grow">
          <h1>{t('Administration')}</h1>
          <p className="sub">
            <RichMessage
              source={'Signed in as {slot0} ({slot1}). Every administrative change is recorded.'}
              slots={{ slot0: account.displayName, slot1: statusLabel(account.role) }}
            />
          </p>
        </div>
      </div>
      <nav
        className="seg"
        aria-label={t('Administration sections')}
        style={{ marginBottom: 'var(--sp-4)' }}
      >
        {TABS.filter(
          (t) => !['overview', 'operations', 'finances'].includes(t.id) || account.role === 'admin',
        ).map((t) => (
          <Link
            key={t.id}
            to={`/admin/${t.id}`}
            aria-current={current === t.id ? 'page' : undefined}
            className={current === t.id ? 'on' : ''}
          >
            {t.name}
          </Link>
        ))}
      </nav>
      {current === 'accounts' && <Accounts isAdmin={account.role === 'admin'} />}
      {current === 'matches' && <Matches />}
      {current === 'reports' && <UnifiedReports />}
      {current === 'skins' && <SkinReports />}
      {current === 'music' && <MusicReports />}
      {current === 'content' && <Content />}
      {current === 'audit' && <Audit />}
      {current === 'overview' &&
        (account.role === 'admin' ? <Overview /> : <p>{t('Administrator access is required.')}</p>)}
      {current === 'finances' &&
        (account.role === 'admin' ? <Finances /> : <p>{t('Administrator access is required.')}</p>)}
      {current === 'operations' &&
        (account.role === 'admin' ? (
          <Operations />
        ) : (
          <p>{t('Administrator access is required.')}</p>
        ))}
    </div>
  );
}
