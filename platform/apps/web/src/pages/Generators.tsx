import { useEffect, useState, type FormEvent } from 'react';
import {
  simVersionKey,
  passedGeneratorReport,
  type GeneratorUpload,
  type GeneratorSettings,
  type GeneratorVersion,
} from '@glob2/protocol';
import { generatorApi } from '../generatorApi.ts';
import { Link, useRouter } from '../router.tsx';
import { useLoad, useSession } from '../state.tsx';
import { Loaded, ErrorNotice } from '../components/common.tsx';
import { date } from '../format.ts';
export function MapLibraryTabs({ generators = false }: { generators?: boolean }) {
  return (
    <nav aria-label="Map library">
      <Link to="/maps" aria-current={!generators ? 'page' : undefined}>
        Maps
      </Link>
      {' · '}
      <Link to="/generators" aria-current={generators ? 'page' : undefined}>
        Generators
      </Link>
    </nav>
  );
}
export function Generators({ mine = false }: { mine?: boolean }) {
  const [q, setQ] = useState(''),
    [tags, setTags] = useState(''),
    [sort, setSort] = useState('newest'),
    [editorOnly, setEditorOnly] = useState(''),
    [simVersion, setSimVersion] = useState('');
  const [cursor, setCursor] = useState<string | undefined>();
  const session = useSession();
  const load = useLoad(
    (signal) =>
      generatorApi.list(
        { q, tags, sort, editorOnly, simVersion, ...(mine ? { owner: 'me' } : {}), cursor },
        signal,
      ),
    [q, tags, sort, editorOnly, simVersion, mine, cursor],
  );
  return (
    <main style={{ overflowWrap: 'anywhere', minWidth: 0 }}>
      <MapLibraryTabs generators />
      <h1>{mine ? 'My generators' : 'Map generators'}</h1>
      <p>
        Generate new landscapes from community JavaScript packages. Install a release in Settings →
        Map generators, or select it in an online room.
      </p>
      <nav>
        <Link to="/generators">Browse</Link>
        {' · '}
        <Link to="/generators/mine">My generators</Link>
        {' · '}
        <Link className="btn" to="/generator-studio">
          Create with AI
        </Link>
        <Link className="btn primary" to="/generators/new">
          Share a generator
        </Link>
      </nav>
      {mine && !session.account ? (
        <p>
          <a href="/signin">Sign in</a> to see your generators.
        </p>
      ) : (
        <>
          <label className="field">
            Search
            <input
              value={q}
              onChange={(e) => {
                setQ(e.target.value);
                setCursor(undefined);
              }}
            />
          </label>
          <label className="field">
            Tags
            <input
              placeholder="terrain:natural,feature:lakes"
              value={tags}
              onChange={(e) => {
                setTags(e.target.value);
                setCursor(undefined);
              }}
            />
          </label>
          <label className="field">
            Sort
            <select
              value={sort}
              onChange={(e) => {
                setSort(e.target.value);
                setCursor(undefined);
              }}
            >
              <option value="newest">Newest</option>
              <option value="updated">Updated</option>
              <option value="likes">Likes</option>
              <option value="downloads">Downloads</option>
            </select>
          </label>
          <label className="field">
            Type
            <select
              value={editorOnly}
              onChange={(e) => {
                setEditorOnly(e.target.value);
                setCursor(undefined);
              }}
            >
              <option value="">All generators</option>
              <option value="false">Playable</option>
              <option value="true">Editor tools</option>
            </select>
          </label>
          <label className="field">
            Engine version
            <select
              value={simVersion}
              onChange={(e) => {
                setSimVersion(e.target.value);
                setCursor(undefined);
              }}
            >
              <option value="">All engine versions</option>
              {session.instance?.supportedSimVersions.map((s) => (
                <option key={simVersionKey(s)} value={simVersionKey(s)}>
                  Engine {s.versionMinor} · {simVersionKey(s).slice(0, 12)}
                </option>
              ))}
            </select>
          </label>
          <Loaded load={load}>
            {(page) => (
              <>
                <div className="map-grid">
                  {page.items.map((g) => (
                    <article key={g.id} className="map-card">
                      <Link to={'/generators/' + g.id}>
                        <img
                          style={{ width: '100%' }}
                          alt={'Example of ' + g.name}
                          loading="lazy"
                          src={`/api/v1/generators/${g.id}/versions/${g.latestVersion.id}/preview.png`}
                        />
                        <h2>{g.name}</h2>
                      </Link>
                      <p>{g.description}</p>
                      <p>
                        {g.latestVersion.metadata.editorOnly ? 'Editor tool' : 'Playable generator'}{' '}
                        · revision {g.latestVersion.metadata.revision} · {g.owner.displayName}
                      </p>
                      <p>
                        {g.tags.join(' · ')} · {g.likes} likes
                      </p>
                    </article>
                  ))}
                </div>
                {!page.items.length && <p>No generators match these filters.</p>}
                {page.nextCursor && (
                  <button onClick={() => setCursor(page.nextCursor)}>Next page</button>
                )}
              </>
            )}
          </Loaded>
        </>
      )}
    </main>
  );
}
function Release({ version, id }: { version: GeneratorVersion; id: string }) {
  return (
    <article>
      <h3>
        {version.label} · revision {version.metadata.revision}
      </h3>
      <p>
        {version.notes} · {date(version.createdAt)}
      </p>
      <a className="btn" href={version.downloadUrl}>
        Download exact package
      </a>
      <p>
        Import the downloaded JSON in Settings → Map generators. Online rooms use this exact release
        without requiring other players to install it.
      </p>
      <img
        style={{ width: '100%', maxWidth: 512 }}
        alt="Generated example map"
        loading="lazy"
        src={`/api/v1/generators/${id}/versions/${version.id}/preview.png`}
      />
      <details>
        <summary>Controls and compatibility evidence</summary>
        <p>
          Canonical package SHA-256: <code>{version.packageHash}</code>
        </p>
        <p>
          Download SHA-256: <code>{version.hash}</code>
        </p>
        <ul>
          {version.metadata.controls.map((c) => (
            <li key={c.id}>
              {c.label}: default {c.default}
              {c.kind === 'choice'
                ? ` (${c.choices?.join(', ')})`
                : c.kind === 'range'
                  ? ` (${c.minimum}–${c.maximum}, step ${c.step})`
                  : ''}
            </li>
          ))}
        </ul>
        {version.validations.map((r, i) => (
          <section key={i}>
            <h4>
              {r.simVersion}:{' '}
              {passedGeneratorReport(r) ? 'Technical checks passed' : 'Checks failed'}
            </h4>
            <p>
              API {version.metadata.apiVersion}, toolkit {version.metadata.toolkitVersion}, suite{' '}
              {r.suite}. These checks sample settings and do not certify balance.
            </p>
            <ul>
              {r.samples.map((s, j) => (
                <li key={j}>
                  {s.status} · seed {s.settings.seed} ·{' '}
                  {Object.entries(s.settings.params)
                    .map(([k, v]) => `${k}=${v}`)
                    .join(', ')}
                  {s.message ? ' · ' + s.message : ''}
                </li>
              ))}
            </ul>
          </section>
        ))}
      </details>
    </article>
  );
}
export function GeneratorPage({ id }: { id: string }) {
  const load = useLoad((signal) => generatorApi.detail(id, signal), [id]);
  const [error, setError] = useState<Error>(),
    [reason, setReason] = useState('broken'),
    [details, setDetails] = useState('');
  const { navigate } = useRouter();
  const session = useSession();
  const action = (work: Promise<unknown>) => {
    setError(undefined);
    void work.then(() => load.reload(), setError);
  };
  return (
    <main style={{ overflowWrap: 'anywhere', minWidth: 0 }}>
      <MapLibraryTabs generators />
      {error && <ErrorNotice error={error} />}
      <Loaded load={load}>
        {(detail) => (
          <>
            <h1>{detail.generator.name}</h1>
            <p>{detail.generator.description}</p>
            <p>
              By {detail.generator.owner.displayName} ·{' '}
              {detail.generator.latestVersion.metadata.editorOnly
                ? 'Editor tool'
                : 'Playable generator'}{' '}
              · {detail.generator.visibility}
            </p>
            {detail.generator.hidden && <p>Hidden: {detail.generator.hiddenReason}</p>}
            {session.account && (
              <>
                <button
                  onClick={() => action(generatorApi.social(id, 'like', !detail.generator.liked))}
                >
                  {detail.generator.liked ? 'Unlike' : 'Like'} ({detail.generator.likes})
                </button>
                <button
                  onClick={() =>
                    action(generatorApi.social(id, 'favourite', !detail.generator.favourited))
                  }
                >
                  {detail.generator.favourited ? 'Remove favourite' : 'Favourite'}
                </button>
              </>
            )}
            {detail.viewer.owner && (
              <>
                <Link to={'/generators/' + id + '/new'}>Publish a new release</Link>
                <Link
                  to={
                    '/generator-studio?version=' +
                    detail.generator.latestVersion.id +
                    '&library=' +
                    id
                  }
                >
                  Edit in Generator Studio
                </Link>
                <label className="field">
                  Visibility
                  <select
                    value={detail.generator.visibility}
                    onChange={(e) =>
                      action(generatorApi.update(id, { visibility: e.target.value }))
                    }
                  >
                    <option value="unlisted">Unlisted</option>
                    <option value="private">Private</option>
                    <option value="public">Public</option>
                  </select>
                </label>
                <button
                  onClick={() => {
                    if (
                      window.confirm(
                        'Remove this generator from the library? Existing matches retain their maps.',
                      )
                    )
                      void generatorApi
                        .remove(id)
                        .then(() => navigate('/generators/mine'), setError);
                  }}
                >
                  Remove generator
                </button>
              </>
            )}
            {detail.versions.map((v) => (
              <Release key={v.id} version={v} id={id} />
            ))}
            {session.account && (
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  action(generatorApi.report(id, reason, details));
                }}
              >
                <h2>Report this generator</h2>
                <label className="field">
                  Reason
                  <select value={reason} onChange={(e) => setReason(e.target.value)}>
                    <option value="broken">Broken</option>
                    <option value="offensive">Offensive</option>
                    <option value="copyright">Copyright</option>
                    <option value="other">Other</option>
                  </select>
                </label>
                <label className="field">
                  Details
                  <textarea
                    required
                    maxLength={4000}
                    value={details}
                    onChange={(e) => setDetails(e.target.value)}
                  />
                </label>
                <button>Send report</button>
              </form>
            )}
          </>
        )}
      </Loaded>
    </main>
  );
}
export function GeneratorPublish({ id }: { id?: string }) {
  const session = useSession(),
    { navigate } = useRouter();
  const [file, setFile] = useState<File>(),
    [name, setName] = useState(''),
    [description, setDescription] = useState(''),
    [version, setVersion] = useState(''),
    [notes, setNotes] = useState(''),
    [visibility, setVisibility] = useState('unlisted');
  const [example, setExample] = useState<GeneratorSettings>({
      seed: 19,
      params: { width: 7, height: 7, teams: 4, workers: 4 },
      candidates: 1,
      startingUnitLevel: 0,
    }),
    [options, setOptions] = useState('{}');
  const [upload, setUpload] = useState<GeneratorUpload>(),
    [busy, setBusy] = useState(false),
    [error, setError] = useState<Error>();
  useEffect(() => {
    if (upload?.status !== 'pending') return;
    const controller = new AbortController();
    const timer = setTimeout(() => {
      void generatorApi.check(upload.id, controller.signal).then(setUpload, (e) => {
        if (!controller.signal.aborted) setError(e instanceof Error ? e : new Error(String(e)));
      });
    }, 1500);
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  }, [upload]);
  const changed = () => setUpload(undefined);
  const validate = async () => {
    if (!file) return;
    setBusy(true);
    setError(undefined);
    try {
      const params = JSON.parse(options) as Record<string, number>;
      setUpload(
        await generatorApi.upload(file, { ...example, params: { ...example.params, ...params } }),
      );
    } catch (e) {
      setError(e instanceof Error ? e : new Error(String(e)));
    } finally {
      setBusy(false);
    }
  };
  const publish = async (e: FormEvent) => {
    e.preventDefault();
    if (!upload) return;
    setBusy(true);
    try {
      const g = await generatorApi.publish(
        { uploadId: upload.id, name, description, version, notes, visibility },
        id,
      );
      navigate('/generators/' + g.id);
    } catch (e) {
      setError(e instanceof Error ? e : new Error(String(e)));
    } finally {
      setBusy(false);
    }
  };
  return (
    <main style={{ overflowWrap: 'anywhere', minWidth: 0 }}>
      <MapLibraryTabs generators />
      <h1>{id ? 'Publish a generator release' : 'Share a map generator'}</h1>
      <p>
        Upload the portable JSON exported from Settings → Map generators.{' '}
        <a href="https://github.com/Globulation2/glob2/blob/master/docs/map-generators/JAVASCRIPT.md">
          Authoring guide
        </a>
      </p>
      {error && <ErrorNotice error={error} />}
      {!session.account ? (
        <p>
          <a href="/signin">Sign in</a> to publish.
        </p>
      ) : (
        <form onSubmit={(e) => void publish(e)}>
          <label className="field">
            Package
            <input
              type="file"
              accept=".json,application/json"
              disabled={busy || upload?.status === 'pending'}
              onChange={(e) => {
                setFile(e.target.files?.[0]);
                changed();
              }}
            />
          </label>
          <fieldset disabled={busy || upload?.status === 'pending'}>
            <legend>Example generation settings</legend>
            {(['width', 'height', 'teams', 'workers'] as const).map((k) => (
              <label className="field" key={k}>
                {k}
                {k === 'width' || k === 'height' ? ' (log₂ tiles)' : ''}
                <input
                  type="number"
                  required
                  value={example.params[k]}
                  onChange={(e) => {
                    setExample({
                      ...example,
                      params: { ...example.params, [k]: Number(e.target.value) },
                    });
                    changed();
                  }}
                />
              </label>
            ))}
            <label className="field">
              Seed
              <input
                type="number"
                min={0}
                max={4294967295}
                value={example.seed}
                onChange={(e) => {
                  setExample({ ...example, seed: Number(e.target.value) });
                  changed();
                }}
              />
            </label>
            <label className="field">
              Generator control values (JSON)
              <textarea
                value={options}
                onChange={(e) => {
                  setOptions(e.target.value);
                  changed();
                }}
              />
            </label>
          </fieldset>
          <button
            type="button"
            disabled={!file || busy || upload?.status === 'pending'}
            onClick={() => void validate()}
          >
            {upload?.status === 'error' ? 'Retry validation' : 'Validate package'}
          </button>
          {upload && (
            <section role="status">
              <h2>
                {upload.status === 'pending'
                  ? 'Checking generator…'
                  : upload.status === 'valid'
                    ? 'Technical checks passed'
                    : 'Validation did not pass'}
              </h2>
              <p>{upload.error ?? upload.report.error}</p>
              <p>Checks cover the listed settings and do not certify game balance.</p>
              <ul>
                {upload.report.samples.map((s, i) => (
                  <li key={i}>
                    {s.status} · seed {s.settings.seed} ·{' '}
                    {s.message ?? (s.fingerprint ? `world ${s.fingerprint.slice(0, 12)}` : '')}
                  </li>
                ))}
              </ul>
            </section>
          )}
          <label className="field">
            Title
            <input
              required
              maxLength={128}
              value={name}
              onChange={(e) => setName(e.target.value)}
            />
          </label>
          <label className="field">
            Description
            <textarea
              maxLength={4000}
              value={description}
              onChange={(e) => setDescription(e.target.value)}
            />
          </label>
          <label className="field">
            Release label
            <input
              required
              maxLength={64}
              value={version}
              onChange={(e) => setVersion(e.target.value)}
            />
          </label>
          <label className="field">
            Release notes
            <textarea maxLength={2000} value={notes} onChange={(e) => setNotes(e.target.value)} />
          </label>
          <label className="field">
            Visibility
            <select value={visibility} onChange={(e) => setVisibility(e.target.value)}>
              <option value="unlisted">Unlisted</option>
              <option value="private">Private</option>
              <option value="public">Public</option>
            </select>
          </label>
          <button
            disabled={busy || upload?.status !== 'valid' || !passedGeneratorReport(upload.report)}
          >
            Publish release
          </button>
        </form>
      )}
    </main>
  );
}
