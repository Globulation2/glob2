import { projectConversation } from '../components/studio/conversation.ts';
import {
  StudioShell,
  StudioHeader,
  StudioWorkspace,
  ChatComposer,
  ConversationPane,
  StudioTabs,
  ReleaseDialog,
  NewStudio,
} from '../components/studio/Studio.tsx';
import { studioSession, useStudioValue } from '../components/studio/storage.ts';
import { Icon } from '../icons.tsx';
import { lazy, Suspense, useCallback, useEffect, useRef, useState } from 'react';
import type {
  StudioAccount,
  AiStudioCommand,
  StudioProject,
  StudioRevision,
  AiValidationReport,
  AiInfo,
} from '@glob2/protocol';
import { ApiError, request } from '../api.ts';
import { checkoutAttempt, checkoutKey } from '../components/studio/checkoutAttempt.ts';
import { useProjectDraft } from '../components/studio/useProjectDraft.ts';
import { useStudioChecks } from '../components/studio/useStudioChecks.ts';
import { generatorApi } from '../generatorApi.ts';
import {
  generatorPackage,
  importGeneratorStudioFile,
  type GeneratorSettings,
  type GeneratorValidationReport,
  type GeneratorInfo,
} from '@glob2/protocol';
import GeneratorSettingsEditor, {
  defaultSettings,
  effectiveSettings,
} from './generatorStudio/Settings.tsx';
import GeneratorPreview, { type GeneratorRun } from './generatorStudio/Preview.tsx';
import { aiApi } from '../aiApi.ts';
import { useSession } from '../state.tsx';
import { Link, useRouter } from '../router.tsx';
import { AiChecklist } from './Ais.tsx';
import Playtest, { type Run } from './aiStudio/Playtest.tsx';
import '../styles/ai-studio.css';
const Editor = lazy(() => import('./aiStudio/Editor.tsx'));
const GeneratorEditor = lazy(() => import('./generatorStudio/Editor.tsx'));
interface Check {
  report: AiValidationReport | GeneratorValidationReport;
  status: string;
  error: string | null;
  upload_id: string | null;
  expires_at: string | null;
  revision?: number;
  draft_hash?: string;
}
function download(source: string, generator = false) {
  let filename = 'ai.js';
  if (generator) {
    try {
      source = generatorPackage(source);
      filename = 'generator.json';
    } catch {
      filename = 'generator-draft.json';
    }
  }
  const url = URL.createObjectURL(
    new Blob([source], { type: generator ? 'application/json' : 'application/javascript' }),
  );
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
export function CodingStudio({ id, generator = false }: { id?: string; generator?: boolean }) {
  const { account } = useSession();
  return (
    <CodingWorkspace
      key={`${account?.id ?? 'anonymous'}:${id ?? 'new'}`}
      id={id}
      generator={generator}
    />
  );
}
function CodingWorkspace({ id, generator }: { id?: string; generator: boolean }) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(timer);
  }, []);
  const route = generator ? 'generator-studio' : 'ai-studio';
  const ROOT = '/api/v1/' + route;
  const creditName = generator ? 'Generator' : 'Colony AI';
  const [settings, setSettings] = useState<GeneratorSettings>(defaultSettings);
  const [generatorRun, setGeneratorRun] = useState<GeneratorRun>();
  const hidePreview = useCallback(() => setGeneratorRun(undefined), []);
  const { account } = useSession(),
    { location, navigate } = useRouter();
  const [wallet, setWallet] = useState<StudioAccount>(),
    [projects, setProjects] = useState<StudioProject[]>([]);
  const [prompt, setPrompt] = useStudioValue(`${route}-prompt:${account?.id}:${id ?? 'new'}`, ''),
    [title, setTitle] = useState(generator ? 'My landscape' : 'My Colony'),
    [error, setError] = useState(''),
    [busy, setBusy] = useState(false);
  const [tab, setTab] = useState<'code' | 'changes' | 'playtest'>('code'),
    [selected, setSelected] = useState(0),
    [baseline, setBaseline] = useState(''),
    [budget, setBudget] = useStudioValue(`${route}-cap:${account?.id}:${id ?? 'new'}`, 0),
    [run, setRun] = useState<Run>(),
    [seed, setSeed] = useState(19),
    [opponent, setOpponent] = useState('numbi'),
    [runResult, setRunResult] = useState(''),
    [diagnostics, setDiagnostics] = useStudioValue(
      `${route}-diagnostics:${account?.id}:${id ?? 'new'}`,
      '',
    ),
    [publishing, setPublishing] = useState(false),
    [release, setRelease] = useState('1.0'),
    [description, setDescription] = useState(''),
    [visibility, setVisibility] = useState<'private' | 'unlisted' | 'public'>('private'),
    [published, setPublished] = useState<AiInfo | GeneratorInfo>();
  const [small, setSmall] = useState(() => window.matchMedia('(max-width: 760px)').matches);
  const [creditsOpen, setCreditsOpen] = useState(false);
  const [focusChat, setFocusChat] = useState(0);
  const [releaseRevision, setReleaseRevision] = useState<number>();
  const [generatedUndo, setGeneratedUndo] = useStudioValue<{ base: number; id: string } | null>(
    `${route}-undo:${account?.id}:${id}`,
    null,
  );
  const form = useRef<HTMLTextAreaElement>(null);
  const url = id ? ROOT + '/projects/' + id : '';
  const recoveryKey = account && id ? `${route}-draft:${account.id}:${id}` : '';
  const {
    project,
    source,
    saved,
    conflict,
    known,
    draft,
    saving,
    cursor,
    change,
    refresh: refreshDraft,
    save,
    reload,
  } = useProjectDraft(url, recoveryKey, setError);
  const {
    checks,
    refresh: refreshChecks,
    invalidate: invalidateChecks,
  } = useStudioChecks<Check>(url, known);
  const recoveryLoaded = useRef(false);
  const submission = useRef<AiStudioCommand | undefined>(undefined);
  const purchases = useRef(new Map<string, { id: string; pack: string }>());
  const checkoutReturn = useRef('');
  const serverTitle = useRef<string | undefined>(undefined);
  const comparisonRequest = useRef(0);
  const currentRevision = () => {
    if (!known.current) throw Error('Project has not loaded.');
    return known.current.revision;
  };
  const {
    messages: conversation,
    terminal,
    pending,
  } = projectConversation(project?.requests ?? []);
  const active = pending?.status === 'uncertain' ? undefined : pending;
  const locked = busy || !!active || conflict;
  const loadWallet = useCallback(async () => {
    const w = await request<StudioAccount>('GET', ROOT + '/account');
    setWallet(w);
    setBudget((b) => b || w.maxRequestCredits);
    return w;
  }, [setBudget, ROOT]);
  const refresh = useCallback(async () => {
    if (!id) return;
    const value = await refreshDraft();
    if (!value) return;
    if (!recoveryLoaded.current && recoveryKey) {
      recoveryLoaded.current = true;
      try {
        const pending = JSON.parse(
          studioSession.getItem(recoveryKey + ':request') ?? 'null',
        ) as AiStudioCommand | null;
        if (pending && !value.requests.some((r) => r.id === pending.id)) {
          submission.current = pending;
          setPrompt(pending.text);
          setBudget(pending.budget);
          setDiagnostics(pending.diagnostics ?? '');
          setError(
            'A previous submission was not acknowledged. Sending it again safely retries the same request.',
          );
        } else studioSession.removeItem(recoveryKey + ':request');
      } catch {
        /* Private browsing may disable local recovery. */
      }
    }
    if (submission.current && value.requests.some((r) => r.id === submission.current?.id)) {
      const sent = submission.current;
      submission.current = undefined;
      setPrompt((text) => (text === sent.text ? '' : text));
      setDiagnostics((text) => (text === (sent.diagnostics ?? '') ? '' : text));
      try {
        studioSession.removeItem(recoveryKey + ':request');
      } catch {
        /* Optional recovery. */
      }
    }
    const previousTitle = serverTitle.current;
    serverTitle.current = value.title;
    setTitle((local) =>
      previousTitle === undefined || local === previousTitle ? value.title : local,
    );
    await refreshChecks();
  }, [id, recoveryKey, refreshDraft, refreshChecks, setBudget, setDiagnostics, setPrompt]);
  useEffect(() => {
    if (
      !account ||
      !wallet ||
      !['returned', 'cancelled'].includes(location.search.get('payment') ?? '')
    )
      return;
    const returned = `${account.id}:${location.search.get('payment')}`;
    if (checkoutReturn.current === returned) return;
    checkoutReturn.current = returned;
    purchases.current.clear();
    for (const pack of wallet.packs) {
      try {
        studioSession.removeItem(checkoutKey(account.id, pack.id, route));
      } catch {
        /* Optional persistence. */
      }
    }
    const search = new URLSearchParams(location.search);
    search.delete('payment');
    navigate(location.path + (search.size ? '?' + search.toString() : ''), { replace: true });
  }, [account, wallet, location.search, location.path, navigate, route]);
  useEffect(() => {
    let mounted = true;
    void (async () => {
      if (!account) return;
      try {
        await loadWallet();
        if (!mounted) return;
        if (id) await refresh();
        else
          setProjects((await request<{ items: StudioProject[] }>('GET', ROOT + '/projects')).items);
      } catch (e) {
        if (mounted) setError(String(e));
      }
    })();
    return () => {
      mounted = false;
    };
  }, [account, id, loadWallet, refresh, ROOT]);
  useEffect(() => {
    const m = window.matchMedia('(max-width: 760px)');
    const update = () => setSmall(m.matches);
    m.addEventListener('change', update);
    return () => m.removeEventListener('change', update);
  }, []);
  useEffect(() => {
    if (!id) return;
    let stopped = false,
      pending = false;
    const timer = setInterval(() => {
      if (pending || stopped || saving.current) return;
      pending = true;
      void request<{ events: { cursor: string }[] }>('GET', url + '/events', {
        query: { after: cursor.current },
      })
        .then(async (v) => {
          if (stopped) return;
          if (
            v.events.length ||
            known.current?.requests.some((r) =>
              ['queued', 'running', 'uncertain'].includes(r.status),
            )
          ) {
            await refresh();
            await loadWallet();
          } else if (checks.some((c) => c.status === 'pending')) await refreshChecks();
        })
        .catch((e) => {
          if (!stopped) setError('Reconnecting: ' + String(e));
        })
        .finally(() => {
          pending = false;
        });
    }, 1500);
    return () => {
      stopped = true;
      clearInterval(timer);
    };
  }, [id, url, refresh, refreshChecks, loadWallet, checks, cursor, known, saving]);
  useEffect(() => {
    if (!project || locked || source === project.current.source) return;
    const timer = setTimeout(() => void save().catch(() => {}), 900);
    return () => clearTimeout(timer);
  }, [source, project, locked, save]);
  const action = async (fn: () => Promise<void>) => {
    setBusy(true);
    setError('');
    try {
      await fn();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };
  const create = async (file?: File) =>
    action(async () => {
      if (file && file.size > (generator ? 262144 : 131072))
        throw Error(
          generator
            ? 'The generator package must fit the 256 KiB draft limit.'
            : 'The single JavaScript file must be at most 128 KiB.',
        );
      const versionId = location.search.get('version');
      const p = await request<StudioProject>('POST', ROOT + '/projects', {
        body: {
          title,
          ...(file
            ? {
                source: generator
                  ? importGeneratorStudioFile(
                      new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer()),
                    )
                  : new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer()),
              }
            : versionId
              ? { versionId }
              : {}),
        },
      });
      if (prompt.trim() && !file) {
        const command = {
          id: crypto.randomUUID(),
          expectedRevision: p.revision,
          text: prompt,
          budget,
        };
        studioSession.setItem(
          `${route}-draft:${account?.id}:${p.id}:request`,
          JSON.stringify(command),
        );
        try {
          await request('POST', `${ROOT}/projects/${p.id}/requests`, { body: command });
          studioSession.removeItem(`${route}-draft:${account?.id}:${p.id}:request`);
        } finally {
          navigate('/' + route + '/' + p.id);
        }
      } else navigate('/' + route + '/' + p.id);
    });
  const revision = async (n: number) => {
    const generation = ++comparisonRequest.current;
    const source = n
      ? (await request<StudioRevision>('GET', url + '/revision', { query: { revision: n } })).source
      : '';
    if (generation !== comparisonRequest.current) return;
    setSelected(n);
    setBaseline(source);
  };
  const restore = async (n: number, confirm = true) =>
    action(async () => {
      if (
        confirm &&
        n !== project?.revision &&
        !window.confirm(
          'Restore this version as the current draft? The current version stays in history.',
        )
      )
        return;
      await save();
      await request('PATCH', url, {
        body: { expectedRevision: currentRevision(), restoreRevision: n },
      });
      await refresh();
      setFocusChat((n) => n + 1);
    });
  const send = async () =>
    action(async () => {
      let command = submission.current;
      if (!command) {
        await save();
        command = {
          id: crypto.randomUUID(),
          expectedRevision: currentRevision(),
          text: prompt,
          budget,
          ...(diagnostics ? { diagnostics } : {}),
        };
        setGeneratedUndo({ base: command.expectedRevision, id: command.id });
        submission.current = command;
        try {
          studioSession.setItem(recoveryKey + ':request', JSON.stringify(command));
        } catch {
          /* In-memory retries remain idempotent. */
        }
      }
      try {
        await request('POST', url + '/requests', { body: command });
      } catch (e) {
        if (e instanceof ApiError && e.status >= 400 && e.status < 500 && e.status !== 408) {
          submission.current = undefined;
          studioSession.removeItem(recoveryKey + ':request');
        }
        throw e;
      }
      submission.current = undefined;
      try {
        studioSession.removeItem(recoveryKey + ':request');
      } catch {
        /* Optional recovery. */
      }
      const sent = command;
      setPrompt((text) => (text === sent.text ? '' : text));
      setDiagnostics((text) => (text === (sent.diagnostics ?? '') ? '' : text));
      await refresh();
    });
  const generate = async () =>
    action(async () => {
      await save();
      const next = await request<GeneratorRun>('POST', url + '/runs', {
        body: {
          id: crypto.randomUUID(),
          expectedRevision: currentRevision(),
          settings: effectiveSettings(source, settings),
        },
      });
      setGeneratorRun(next);
      setRunResult('');
      setTab('playtest');
    });
  const play = async () =>
    action(async () => {
      await save();
      const r = await request<Run>('POST', url + '/runs', {
        body: {
          id: crypto.randomUUID(),
          expectedRevision: currentRevision(),
          seed,
          opponent,
        },
      });
      setRun(r);
      setRunResult('');
      setTab('playtest');
    });
  const currentCheck = checks.find((c) =>
    generator
      ? c.draft_hash === project?.current.hash && c.revision === project?.revision
      : c.report.sourceHash === project?.current.hash,
  );
  const fix = (text: string) => {
    setDiagnostics(text.slice(0, 16000));
    setPrompt('Please fix the issues in the attached test diagnostics.');
    setFocusChat((n) => n + 1);
    form.current?.focus();
  };
  const publish = async () =>
    action(async () => {
      if (
        project?.revision !== releaseRevision ||
        !currentCheck?.upload_id ||
        currentCheck.status !== 'valid' ||
        source !== project?.current.source
      )
        throw Error('Run compatibility checks on the current saved revision first.');
      const body = {
        uploadId: currentCheck.upload_id,
        name: title,
        description,
        visibility,
        version: generator
          ? String((currentCheck.report as GeneratorValidationReport).metadata?.revision)
          : release,
        notes: `Created in ${generator ? 'Generator' : 'AI'} Studio, revision ${project.revision}`,
      };
      if (generator) {
        if (!currentCheck.expires_at || Date.parse(currentCheck.expires_at) <= now)
          throw Error('The validation receipt expired. Run checks again.');
        const manifestId = (currentCheck.report as GeneratorValidationReport).metadata?.id;
        let cursor: string | undefined, target: string | undefined;
        do {
          const page = await generatorApi.list({ owner: 'me', limit: 100, cursor });
          target = page.items.find((g) => g.latestVersion.metadata.id === manifestId)?.id;
          cursor = page.nextCursor;
        } while (!target && cursor);
        setPublished(await generatorApi.publish(body, target));
      } else setPublished(await aiApi.publish({ ...body, tags: [] }));
      setPublishing(false);
    });
  if (!account)
    return (
      <div className="notice">
        <h1>{generator ? 'Generator Studio' : 'AI Studio'}</h1>
        <p>
          {generator
            ? 'Create a map generator through conversation.'
            : 'Create a colony AI through conversation.'}
        </p>
        <a href="/signin" className="btn primary">
          Sign in
        </a>
      </div>
    );
  return (
    <StudioShell className="as-page">
      <StudioHeader
        title={project?.title ?? (generator ? 'Generator Studio' : 'AI Colony Studio')}
        icon="robot"
      >
        <Link to={'/' + route}>
          <Icon name="folder-open" size={18} /> Projects
        </Link>
        <span role="status">
          {id ? `${saved} · revision ${project?.revision ?? '…'}` : 'Private project'}
        </span>
        <button onClick={() => setCreditsOpen(true)}>
          <Icon name="coins" size={18} /> {wallet?.available ?? '…'} {creditName} credits
        </button>
      </StudioHeader>
      {project && !source.trim() && (
        <p role="status">Draft is temporarily empty. Add source to save.</p>
      )}
      {wallet && !wallet.enabled && (
        <p role="status">
          {generator ? (
            <>
              Generator Studio is disabled on this instance. Recover saved files through{' '}
              <Link to="/account">account export</Link>.
            </>
          ) : (
            'Generation is unavailable. Saved code, export and local tools remain accessible.'
          )}
        </p>
      )}

      {error && (
        <div role="alert" className="notice">
          {error}
          {conflict && (
            <>
              <button onClick={() => download(source, generator)}>Download local edits</button>
              <button
                onClick={() =>
                  void action(async () => {
                    await reload();
                    await refresh();
                  })
                }
              >
                Reload saved revision
              </button>
            </>
          )}
        </div>
      )}
      {!id ? (
        <NewStudio
          title={generator ? 'Your landscape' : 'Your colony AI'}
          value={prompt}
          onChange={setPrompt}
          onSend={() => void create()}
          disabledReason={
            busy
              ? 'Saving your project…'
              : !wallet?.enabled
                ? 'Generation is unavailable.'
                : !wallet.available
                  ? `An available ${creditName} credit is needed.`
                  : undefined
          }
          onBlocked={
            !busy && wallet?.enabled && !wallet.available ? () => setCreditsOpen(true) : undefined
          }
          pricing={`Coding requests use up to ${budget} ${creditName} credits. Manual editing and local playtests are free.`}
          projects={
            <>
              {' '}
              <div className="as-projects">
                {projects.map((p) => (
                  <Link className="card" key={p.id} to={'/' + route + '/' + p.id}>
                    <h2>{p.title}</h2>
                    <p>Revision {p.revision}</p>
                  </Link>
                ))}
              </div>
            </>
          }
          tools={
            <>
              {' '}
              <section className="card">
                <h2>Start a project</h2>
                <label>
                  Project name{' '}
                  <input value={title} maxLength={128} onChange={(e) => setTitle(e.target.value)} />
                </label>
                <button
                  className="primary"
                  disabled={busy || !title.trim() || !wallet?.enabled}
                  onClick={() => void create()}
                >
                  {location.search.has('version')
                    ? 'Copy my library version'
                    : 'Use working starter'}
                </button>
                <label className="btn">
                  {generator ? 'Import package' : 'Import .js'}
                  <input
                    aria-label={generator ? 'Import generator package' : 'Import JavaScript'}
                    type="file"
                    accept={generator ? '.json' : '.js'}
                    disabled={busy || !wallet?.enabled}
                    onChange={(e) => {
                      const file = e.target.files?.[0];
                      if (file) void create(file);
                      e.target.value = '';
                    }}
                  />
                </label>
                <p>
                  {generator ? 'One manifest and one JavaScript module.' : 'One JavaScript file.'}{' '}
                  Manual editing and local playtests use no credits.
                </p>
              </section>
            </>
          }
        />
      ) : (
        project && (
          <>
            {published && (
              <p className="notice">
                Published{' '}
                <Link to={(generator ? '/generators/' : '/ais/') + published.id}>
                  {published.name}
                </Link>
                .
              </p>
            )}
            <ReleaseDialog
              open={publishing}
              onClose={() => setPublishing(false)}
              title={`Publish revision ${releaseRevision}`}
            >
              <form
                className="card"
                onSubmit={(e) => {
                  e.preventDefault();
                  void publish();
                }}
              >
                <h2>Publish revision {releaseRevision}</h2>
                {generator ? (
                  <p>
                    Manifest release revision:{' '}
                    {(currentCheck?.report as GeneratorValidationReport)?.metadata?.revision}.
                    Publishing a new release requires a larger manifest revision.
                  </p>
                ) : (
                  <>
                    {' '}
                    <label>
                      Version{' '}
                      <input
                        required
                        value={release}
                        maxLength={64}
                        onChange={(e) => setRelease(e.target.value)}
                      />
                    </label>
                  </>
                )}
                <label>
                  Description{' '}
                  <textarea
                    value={description}
                    maxLength={4000}
                    onChange={(e) => setDescription(e.target.value)}
                  />
                </label>
                <label>
                  Visibility{' '}
                  <select
                    value={visibility}
                    onChange={(e) => setVisibility(e.target.value as typeof visibility)}
                  >
                    <option value="private">Private</option>
                    <option value="unlisted">Unlisted</option>
                    <option value="public">Public</option>
                  </select>
                </label>
                <p>
                  Destination: {generator ? 'Generator' : 'Colony AI'} library. Run checks before
                  publication.
                </p>
                {project.revision !== releaseRevision && (
                  <p role="alert">
                    The current draft changed. Close this panel and review the new revision.
                  </p>
                )}
                <button disabled={busy || project.revision !== releaseRevision}>Publish</button>
              </form>{' '}
            </ReleaseDialog>
            {generatedUndo &&
              project.requests.some((r) => r.id === generatedUndo.id && r.status === 'completed') &&
              project.revisions.some(
                (r) => r.revision === generatedUndo.base + 1 && r.reason === 'assistant',
              ) && (
                <div className="studio-revisions" role="status">
                  <span>
                    Generated source accepted as revision {generatedUndo.base + 1}. Run engine
                    checks before publication.
                  </span>
                  <button
                    aria-disabled={
                      locked ||
                      project.revision !== generatedUndo.base + 1 ||
                      source !== project.current.source
                    }
                    onClick={() => {
                      if (
                        locked ||
                        project.revision !== generatedUndo.base + 1 ||
                        source !== project.current.source
                      )
                        return;
                      void restore(generatedUndo.base, false);
                    }}
                  >
                    Undo
                  </button>
                  {project.revision !== generatedUndo.base + 1 && (
                    <span>A newer revision prevents undo. Restore a version in Changes.</span>
                  )}
                </div>
              )}
            <StudioWorkspace
              focusChat={focusChat}
              onArtifactHidden={generator ? hidePreview : undefined}
              attention={
                pending?.status === 'uncertain'
                  ? 'Needs reconciliation'
                  : pending
                    ? 'Working'
                    : undefined
              }
              result={
                !pending && terminal
                  ? {
                      id: `${terminal.id}:${terminal.status}`,
                      status: terminal.status === 'completed' ? 'ready' : 'failed',
                      text:
                        terminal.status === 'completed'
                          ? `${creditName} response ready in Preview.`
                          : `${creditName} request needs attention in Preview.`,
                    }
                  : undefined
              }
              conversation={
                <section
                  className="as-chat"
                  aria-label={
                    generator ? 'Generator coding conversation' : 'AI coding conversation'
                  }
                >
                  <ConversationPane
                    label={generator ? 'Generator coding conversation' : 'AI coding conversation'}
                    count={project.requests.length}
                    firstMessageId={conversation[0]?.id}
                    completion={
                      terminal
                        ? {
                            id: `${terminal.id}:${terminal.status}`,
                            text:
                              terminal.status === 'completed'
                                ? 'AI response ready.'
                                : 'AI request needs attention.',
                          }
                        : undefined
                    }
                  >
                    {project.requests.length === 0 && (
                      <div className="as-welcome">
                        <h2>
                          {generator
                            ? 'What landscape will you create?'
                            : 'What kind of colony will you build?'}
                        </h2>
                        <p>
                          {generator
                            ? 'Try “a winding river with fertile banks” or ask how the current generator works.'
                            : 'Try “focus on food before expansion” or ask how the current code works.'}
                        </p>
                        <p>
                          {generator
                            ? 'The assistant edits your manifest and JavaScript together. You decide when to generate or run checks.'
                            : 'The assistant edits your file. You decide when to test.'}
                        </p>
                      </div>
                    )}
                    {conversation.map((r) => (
                      <article key={r.id}>
                        <p className="as-user">{r.prompt}</p>
                        {r.diagnostics && (
                          <details>
                            <summary>Attached diagnostics</summary>
                            <pre>{r.diagnostics}</pre>
                          </details>
                        )}
                        <p className="as-response">
                          {r.response ||
                            ({
                              queued: 'Waiting to start…',
                              running: generator
                                ? 'Working on your landscape…'
                                : 'Working on your AI…',
                            }[r.status] ??
                              r.status)}
                        </p>
                        {r.error && <p role="status">{r.error}</p>}
                        <small>
                          {r.status} ·{' '}
                          {r.charged === null ? 'usage pending' : r.charged + ' credits'}
                        </small>
                      </article>
                    ))}
                  </ConversationPane>
                  <ChatComposer
                    value={prompt}
                    onChange={setPrompt}
                    onSend={() => void send()}
                    inputRef={form}
                    maxLength={16000}
                    label="Describe a change or ask a question"
                    placeholder={
                      generator
                        ? 'Add islands and sheltered starting colonies…'
                        : 'Make my colony more defensive…'
                    }
                    target={`Editing saved revision ${project.revision}`}
                    disabledReason={
                      pending?.status === 'uncertain'
                        ? `The provider outcome needs reconciliation. Your reserved ${creditName} credits remain held; new requests are paused. Manual editing and export remain available.`
                        : locked
                          ? 'Finish active work or resolve the revision conflict first.'
                          : !wallet?.enabled
                            ? 'Generation is unavailable.'
                            : !wallet.available
                              ? `An available ${creditName} credit is needed.`
                              : !Number.isInteger(budget) ||
                                  budget < 1 ||
                                  budget > wallet.maxRequestCredits
                                ? 'Choose a valid request cap.'
                                : undefined
                    }
                    onBlocked={
                      !locked && !pending && wallet?.enabled && !wallet.available
                        ? () => setCreditsOpen(true)
                        : undefined
                    }
                    pricing={`Coding requests use up to ${budget} ${creditName} credits. Source acceptance does not mean engine checks passed.`}
                    tools={
                      <>
                        <label>
                          Request cap{' '}
                          <input
                            type="number"
                            min={1}
                            max={wallet?.maxRequestCredits}
                            value={budget}
                            onChange={(e) => setBudget(Number(e.target.value))}
                          />
                        </label>
                        {diagnostics && (
                          <p>
                            Test diagnostics attached.{' '}
                            <button type="button" onClick={() => setDiagnostics('')}>
                              Remove
                            </button>
                          </p>
                        )}
                      </>
                    }
                  />
                  {pending && (
                    <button
                      aria-disabled={pending.status === 'uncertain'}
                      onClick={() =>
                        pending.status !== 'uncertain' &&
                        void action(async () => {
                          await request('POST', url + '/stop');
                          await refresh();
                        })
                      }
                    >
                      Stop request
                    </button>
                  )}
                  {pending?.status === 'uncertain' && (
                    <p role="status">
                      Cancellation is unavailable while the provider outcome and reserved credits
                      are reconciled. You can keep editing the saved source or export it.
                    </p>
                  )}
                </section>
              }
              artifact={
                <section className="as-code-pane">
                  {' '}
                  <div className="as-toolbar">
                    <label>
                      Project{' '}
                      <input
                        value={title}
                        maxLength={128}
                        disabled={locked}
                        onChange={(e) => setTitle(e.target.value)}
                        onBlur={() => {
                          if (title.trim() && title !== project.title)
                            void action(async () => {
                              await save();
                              await request('PATCH', url, {
                                body: { expectedRevision: currentRevision(), title },
                              });
                              await refresh();
                            });
                        }}
                      />
                    </label>
                    <span role="status">
                      {saved} · revision {project.revision}
                    </span>
                    <button
                      disabled={locked}
                      onClick={() =>
                        void action(async () => {
                          await save();
                          invalidateChecks();
                          await request('POST', url + '/check', {
                            body: {
                              revision: currentRevision(),
                              ...(generator
                                ? { settings: effectiveSettings(source, settings) }
                                : {}),
                            },
                          });
                          await refresh();
                        })
                      }
                    >
                      Run checks
                    </button>
                    <button
                      disabled={locked}
                      onClick={() => void (generator ? generate() : play())}
                    >
                      {generator ? 'Generate' : 'Playtest'}
                    </button>
                    <button onClick={() => download(source, generator)}>Download</button>
                    <button
                      disabled={
                        locked ||
                        source !== project.current.source ||
                        currentCheck?.status !== 'valid' ||
                        (generator &&
                          (!currentCheck.expires_at ||
                            Date.parse(currentCheck.expires_at) <= now)) ||
                        !currentCheck.upload_id
                      }
                      onClick={() => {
                        setReleaseRevision(project.revision);
                        setPublishing(!publishing);
                      }}
                    >
                      Publish
                    </button>
                  </div>
                  <StudioTabs
                    label="Workspace view"
                    panels={{
                      code: 'studio-panel-workspace-view-code',
                      changes: 'studio-panel-workspace-view-changes',
                      playtest: 'studio-panel-workspace-view-playtest',
                    }}
                    value={tab}
                    onChange={(next) => {
                      setTab(next as typeof tab);
                      if (generator && next !== 'playtest') setGeneratorRun(undefined);
                      if (!generator && next === 'playtest' && !run && !locked) void play();
                      if (next === 'changes' && !selected)
                        void action(() =>
                          revision(project.revisions[1]?.revision ?? project.revision),
                        );
                    }}
                    items={[
                      { id: 'code', label: 'Code', icon: 'pencil' },
                      { id: 'changes', label: 'Changes', icon: 'restore' },
                      { id: 'playtest', label: generator ? 'Preview' : 'Playtest', icon: 'flask' },
                    ]}
                  />
                  <div className="as-toolbar">
                    <strong>{generator ? 'Generator package' : 'ai.js'}</strong>
                    <label>
                      History{' '}
                      <select
                        value={selected}
                        onChange={(e) => void action(() => revision(Number(e.target.value)))}
                      >
                        <option value={0}>Choose revision</option>
                        {project.revisions.map((r) => (
                          <option key={r.revision} value={r.revision}>
                            r{r.revision} · {r.reason}
                          </option>
                        ))}
                      </select>
                    </label>
                    <button disabled={locked || !selected} onClick={() => void restore(selected)}>
                      Restore
                    </button>
                    <button
                      disabled={locked || project.revisions.length < 2}
                      onClick={() =>
                        void restore(project.revisions[1]?.revision ?? project.revision)
                      }
                    >
                      Undo revision
                    </button>
                    <label className="btn">
                      Import
                      <input
                        aria-label={
                          generator
                            ? 'Replace generator package'
                            : 'Replace source with JavaScript file'
                        }
                        type="file"
                        accept={generator ? '.json' : '.js'}
                        disabled={locked}
                        onChange={(e) => {
                          const file = e.target.files?.[0];
                          if (file)
                            void action(async () => {
                              if (file.size > (generator ? 262144 : 131072))
                                throw Error(
                                  generator
                                    ? 'Maximum package size is 256 KiB.'
                                    : 'Maximum source size is 128 KiB.',
                                );
                              await save();
                              let source = new TextDecoder('utf-8', { fatal: true }).decode(
                                await file.arrayBuffer(),
                              );
                              if (generator) source = importGeneratorStudioFile(source);
                              await request('PATCH', url, {
                                body: {
                                  expectedRevision: currentRevision(),
                                  source,
                                  reason: 'import',
                                },
                              });
                              await refresh();
                            });
                          e.target.value = '';
                        }}
                      />
                    </label>
                  </div>
                  <div
                    id="studio-panel-workspace-view-code"
                    role="tabpanel"
                    aria-labelledby="studio-tab-workspace-view-code"
                    hidden={tab !== 'code'}
                  >
                    {generator ? (
                      <Suspense fallback={<p>Loading editor…</p>}>
                        <GeneratorEditor
                          source={source}
                          readOnly={locked}
                          onChange={change}
                          small={small}
                        />
                      </Suspense>
                    ) : small ? (
                      <textarea
                        className="as-source"
                        aria-label="AI JavaScript source"
                        spellCheck={false}
                        readOnly={locked}
                        value={source}
                        onChange={(e) => change(e.target.value)}
                      />
                    ) : (
                      <Suspense fallback={<p>Loading code editor…</p>}>
                        <Editor
                          source={source}
                          readOnly={locked}
                          onChange={(text) => {
                            if (text !== draft.current) change(text);
                          }}
                        />
                      </Suspense>
                    )}
                  </div>
                  <div
                    id="studio-panel-workspace-view-changes"
                    role="tabpanel"
                    aria-labelledby="studio-tab-workspace-view-changes"
                    hidden={tab !== 'changes'}
                  >
                    {tab === 'changes' &&
                      (generator ? (
                        <Suspense fallback={<p>Loading comparison…</p>}>
                          <GeneratorEditor
                            source={source}
                            baseline={baseline}
                            readOnly
                            onChange={() => {}}
                            small={small}
                          />
                        </Suspense>
                      ) : small ? (
                        <>
                          <textarea
                            className="as-source"
                            aria-label="Current AI JavaScript source"
                            spellCheck={false}
                            readOnly
                            value={source}
                          />
                          <details>
                            <summary>Compared revision {selected}</summary>
                            <pre>{baseline}</pre>
                          </details>
                        </>
                      ) : (
                        <Suspense fallback={<p>Loading revision comparison…</p>}>
                          <Editor
                            source={source}
                            baseline={baseline}
                            readOnly
                            onChange={() => {}}
                          />
                        </Suspense>
                      ))}
                  </div>
                  <div
                    id="studio-panel-workspace-view-playtest"
                    role="tabpanel"
                    aria-labelledby="studio-tab-workspace-view-playtest"
                    hidden={tab !== 'playtest'}
                  >
                    {generator ? (
                      <>
                        <GeneratorSettingsEditor
                          source={source}
                          value={settings}
                          onChange={setSettings}
                        />
                        <button disabled={locked} onClick={() => void generate()}>
                          Generate
                        </button>
                        {generatorRun && (
                          <button onClick={() => setGeneratorRun(undefined)}>Stop preview</button>
                        )}
                        {generatorRun ? (
                          <GeneratorPreview
                            key={generatorRun.runId}
                            run={generatorRun}
                            onResult={(text) => {
                              setRunResult(text);
                              void request('POST', url + '/run-result', {
                                body: { runId: generatorRun.runId, summary: text },
                              }).catch((e) => setError(String(e)));
                            }}
                          />
                        ) : (
                          <p>
                            Generate a map to inspect its terrain and starting colonies, then watch
                            AI colonies play.
                          </p>
                        )}
                      </>
                    ) : (
                      <>
                        <div className="as-toolbar">
                          <label>
                            Seed{' '}
                            <input
                              type="number"
                              min={0}
                              max={4294967295}
                              value={seed}
                              onChange={(e) => setSeed(Number(e.target.value))}
                            />
                          </label>
                          <label>
                            Opponent{' '}
                            <select value={opponent} onChange={(e) => setOpponent(e.target.value)}>
                              <option value="numbi">Numbi</option>
                              <option value="nicowar">Nicowar</option>
                            </select>
                          </label>
                          <button disabled={locked} onClick={() => void play()}>
                            {run ? 'Run current revision' : 'Start live game'}
                          </button>
                          {run && (
                            <>
                              <button
                                disabled={busy}
                                onClick={() =>
                                  void action(async () => {
                                    const next = await request<Run>('POST', url + '/runs', {
                                      body: {
                                        id: crypto.randomUUID(),
                                        expectedRevision: run.revision,
                                        seed: run.seed,
                                        opponent: run.opponent,
                                      },
                                    });
                                    setRun(next);
                                  })
                                }
                              >
                                Restart same setup
                              </button>
                              <button onClick={() => setRun(undefined)}>Stop game</button>
                            </>
                          )}
                        </div>
                        {run ? (
                          <Playtest
                            key={run.runId}
                            run={run}
                            onResult={(text) => {
                              setRunResult(text);
                              void request('POST', url + '/run-result', {
                                body: { runId: run.runId, summary: text },
                              }).catch((e) => setError(String(e)));
                            }}
                          />
                        ) : (
                          <p className="as-welcome">
                            Watch your AI against a built-in opponent on the fixed test map. The
                            game runs on this computer and ends when you close it.
                          </p>
                        )}
                      </>
                    )}
                    {runResult && (
                      <button onClick={() => fix(runResult)}>
                        {generator ? 'Send diagnostics to chat' : 'Fix this'}
                      </button>
                    )}
                  </div>
                  {currentCheck && (
                    <details className="as-checks" open={currentCheck.status === 'invalid'}>
                      <summary>
                        Revision {project.revision} checks: {currentCheck.status}
                      </summary>
                      {generator ? (
                        <>
                          <p>Technical checks do not establish balance or fun.</p>
                          <pre>{JSON.stringify(currentCheck.report, null, 2)}</pre>
                        </>
                      ) : (
                        <AiChecklist report={currentCheck.report as AiValidationReport} />
                      )}
                      {currentCheck.error && <p>{currentCheck.error}</p>}
                      <button
                        disabled={generator && currentCheck.status === 'pending'}
                        onClick={() => fix(JSON.stringify(currentCheck.report))}
                      >
                        {generator ? 'Send diagnostics to chat' : 'Fix this'}
                      </button>
                    </details>
                  )}
                </section>
              }
            />
            <ReleaseDialog
              open={creditsOpen}
              onClose={() => setCreditsOpen(false)}
              title={creditName + ' credits'}
            >
              {' '}
              <details>
                <summary>
                  {wallet?.available ?? 0} credits available · {wallet?.reserved ?? 0} reserved
                </summary>
                <p>
                  {wallet?.model} · per million tokens: {wallet?.rate?.input} input /{' '}
                  {wallet?.rate?.cachedInput} cached / {wallet?.rate?.output} output credits.
                </p>
                <p>Measured model usage is charged, including code that later fails tests.</p>
                {wallet?.packs.map((p) => (
                  <button
                    key={p.id}
                    onClick={() =>
                      void action(async () => {
                        const key = checkoutKey(account.id, p.id, route);
                        const attempt =
                          purchases.current.get(key) ??
                          checkoutAttempt(account.id, p.id, undefined, route);
                        purchases.current.set(key, attempt);
                        try {
                          const v = await request<{ url: string }>('POST', ROOT + '/checkout', {
                            body: attempt,
                          });
                          window.location.assign(v.url);
                        } catch (e) {
                          if (e instanceof ApiError && e.status === 409) {
                            purchases.current.delete(key);
                            try {
                              studioSession.removeItem(key);
                            } catch {
                              /* Optional persistence. */
                            }
                          }
                          throw e;
                        }
                      })
                    }
                  >
                    {p.credits} credits · {(p.amount / 100).toFixed(2)} {p.currency.toUpperCase()}
                  </button>
                ))}
              </details>
            </ReleaseDialog>

            <details>
              <summary>Project actions</summary>
              <button
                disabled={locked}
                onClick={() =>
                  void action(async () => {
                    if (!window.confirm('Delete this private project and its history?')) return;
                    await request('DELETE', url);
                    navigate('/' + route);
                  })
                }
              >
                Delete project
              </button>
            </details>
          </>
        )
      )}
    </StudioShell>
  );
}
