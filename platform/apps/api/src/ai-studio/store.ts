import { createHash } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import { AI_STUDIO_SOURCE_BYTES, type AiStudioCommand } from '@glob2/protocol';
import { HiveError } from '@glob2/billing';
type Db = Kysely<Database> | Transaction<Database>;
export interface Project {
  id: string;
  account_id: string;
  title: string;
  revision: number;
  updated_at: Date;
}
export interface Revision {
  revision: number;
  source: string;
  hash: string;
  reason: string;
  created_at: Date;
}
export interface StudioRequest {
  id: string;
  project_id: string;
  base_revision: number;
  prompt: string;
  diagnostics: string;
  budget: number;
  status: string;
  response: string;
  cancelled: boolean;
  provider_result: ProviderResult | null;
}
export interface ProviderResult {
  editError?: string;
  text: string;
  source?: string;
  usage: { input: number; cachedInput: number; output: number };
  responseId?: string;
}
export function sourceHash(source: string) {
  if (
    !source ||
    source.includes('\0') ||
    Buffer.byteLength(source, 'utf8') > AI_STUDIO_SOURCE_BYTES ||
    Buffer.from(source).toString('utf8') !== source
  )
    throw new HiveError(
      'bad_request',
      'Use a nonempty UTF-8 JavaScript file of at most 128 KiB without NUL bytes.',
    );
  return createHash('sha256').update(source).digest('hex');
}
export class StudioStore {
  readonly db: Kysely<Database>;
  constructor(db: Kysely<Database>) {
    this.db = db;
  }
  async project(id: string, account: string, db: Db = this.db, lock = false) {
    const p = (
      await sql<Project>`SELECT * FROM ai_studio_projects WHERE id=${id} AND account_id=${account} ${lock ? sql`FOR UPDATE` : sql``}`.execute(
        db,
      )
    ).rows[0];
    if (!p) throw new HiveError('not_found', 'No such AI Studio project.');
    return p;
  }
  async revision(id: string, revision: number, db: Db = this.db) {
    const row = (
      await sql<Revision>`SELECT * FROM ai_studio_revisions WHERE project_id=${id} AND revision=${revision}`.execute(
        db,
      )
    ).rows[0];
    if (!row) throw new HiveError('not_found', 'No such revision.');
    return row;
  }
  async event(db: Db, project: string, kind: string, data: unknown, request?: string) {
    // Serialize per-project event allocation and commit order for resumable cursors.
    await sql`SELECT id FROM ai_studio_projects WHERE id=${project} FOR UPDATE`.execute(db);
    await sql`INSERT INTO ai_studio_events(project_id,request_id,kind,body) VALUES(${project},${request ?? null},${kind},${JSON.stringify(data)}::jsonb)`.execute(
      db,
    );
  }
  async create(account: string, title: string, source: string) {
    const hash = sourceHash(source);
    if (!title.trim() || title.includes('\0'))
      throw new HiveError('bad_request', 'Choose a project name without NUL bytes.');
    return this.db.transaction().execute(async (db) => {
      const owner = await db
        .selectFrom('accounts')
        .select('status')
        .where('id', '=', account)
        .forUpdate()
        .executeTakeFirst();
      if (owner?.status !== 'active') throw new HiveError('not_found', 'No active account.');
      // The account lock serializes this quota across simultaneous creations.
      const count = await db
        .selectFrom('ai_studio_projects')
        .select((eb) => eb.fn.countAll<number>().as('count'))
        .where('account_id', '=', account)
        .executeTakeFirstOrThrow();
      if (Number(count.count) >= 100)
        throw new HiveError(
          'bad_request',
          'The Studio limit is 100 projects. Delete an unused project first.',
        );
      const p = (
        await sql<Project>`INSERT INTO ai_studio_projects(account_id,title) VALUES(${account},${title}) RETURNING *`.execute(
          db,
        )
      ).rows[0];
      if (!p) throw new HiveError('not_found', 'Could not create the project.');
      await sql`INSERT INTO ai_studio_revisions(project_id,revision,source,hash,reason) VALUES(${p.id},1,${source},${hash},'initial')`.execute(
        db,
      );
      return p;
    });
  }
  async append(db: Db, p: Project, source: string, reason: string) {
    const revision = p.revision + 1,
      hash = sourceHash(source);
    await sql`INSERT INTO ai_studio_revisions(project_id,revision,source,hash,reason) VALUES(${p.id},${revision},${source},${hash},${reason})`.execute(
      db,
    );
    await sql`UPDATE ai_studio_projects SET revision=${revision},updated_at=now() WHERE id=${p.id}`.execute(
      db,
    );
    await this.event(db, p.id, 'revision', { revision, reason });
    return revision;
  }
  async save(
    id: string,
    account: string,
    expected: number,
    value: { source?: string; restoreRevision?: number; title?: string; reason?: string },
  ) {
    return this.db.transaction().execute(async (db) => {
      const p = await this.project(id, account, db, true);
      await this.editable(db, p, expected);
      if (value.title !== undefined && (!value.title.trim() || value.title.includes('\0')))
        throw new HiveError('bad_request', 'Invalid project name.');
      if (value.title !== undefined)
        await sql`UPDATE ai_studio_projects SET title=${value.title},updated_at=now() WHERE id=${id}`.execute(
          db,
        );
      const source = value.restoreRevision
        ? (await this.revision(id, value.restoreRevision, db)).source
        : value.source;
      if (source === undefined) return p.revision;
      if (
        value.restoreRevision === undefined &&
        (await this.revision(id, p.revision, db)).source === source
      )
        return p.revision;
      return this.append(
        db,
        p,
        source,
        value.restoreRevision ? 'restore' : (value.reason ?? 'manual'),
      );
    });
  }
  async editable(db: Db, p: Project, expected: number) {
    if (p.revision !== expected)
      throw new HiveError(
        'conflict',
        'This project changed in another tab. Reload before applying your edit.',
      );
    if (
      (
        await sql`SELECT id FROM ai_studio_requests WHERE project_id=${p.id} AND status IN ('queued','running')`.execute(
          db,
        )
      ).rows.length
    )
      throw new HiveError('conflict', 'Wait for the current assistant request to finish.');
  }
  async command(id: string, account: string, input: AiStudioCommand) {
    if (input.text.includes('\0') || input.diagnostics?.includes('\0'))
      throw new HiveError('bad_request', 'Requests cannot contain NUL bytes.');
    return this.db.transaction().execute(async (db) => {
      const p = await this.project(id, account, db, true);
      const prior = (
        await sql<StudioRequest>`SELECT * FROM ai_studio_requests WHERE id=${input.id}`.execute(db)
      ).rows[0];
      if (prior) {
        if (
          prior.project_id !== id ||
          prior.prompt !== input.text ||
          prior.base_revision !== input.expectedRevision ||
          prior.budget !== input.budget ||
          prior.diagnostics !== (input.diagnostics ?? '')
        )
          throw new HiveError('conflict', 'Request identifier was already used.');
        return;
      }
      await this.editable(db, p, input.expectedRevision);
      await sql`INSERT INTO ai_studio_requests(id,project_id,base_revision,prompt,diagnostics,budget) VALUES(${input.id},${id},${input.expectedRevision},${input.text},${input.diagnostics ?? ''},${input.budget})`.execute(
        db,
      );
      await this.event(db, id, 'request', { id: input.id, status: 'queued' }, input.id);
    });
  }
}
