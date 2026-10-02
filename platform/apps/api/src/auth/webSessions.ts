// Web (browser) sessions: a random secret in an HttpOnly, SameSite=Lax cookie,
// stored as its SHA-256. The `__Host-` prefix pins the cookie to this exact
// origin when the instance is served over HTTPS.
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import { randomSecret, sha256Hex } from './secrets.ts';

export class WebSessionService {
  private readonly db: Kysely<Database>;
  private readonly lifetimeSeconds: number;

  constructor(db: Kysely<Database>, lifetimeSeconds: number) {
    this.db = db;
    this.lifetimeSeconds = lifetimeSeconds;
  }

  get maxAgeSeconds(): number {
    return this.lifetimeSeconds;
  }

  async create(accountId: string): Promise<string> {
    const secret = randomSecret();
    await this.db
      .insertInto('web_sessions')
      .values({
        account_id: accountId,
        token_hash: sha256Hex(secret),
        expires_at: new Date(Date.now() + this.lifetimeSeconds * 1000),
      })
      .execute();
    return secret;
  }

  async find(secret: string): Promise<Account | undefined> {
    const row = await this.db
      .updateTable('web_sessions')
      .set({ last_used_at: sql<Date>`now()` })
      .where('token_hash', '=', sha256Hex(secret))
      .where('revoked_at', 'is', null)
      .where('expires_at', '>', sql<Date>`now()`)
      .returning('account_id')
      .executeTakeFirst();
    if (!row) return undefined;
    const account = await this.db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', row.account_id)
      .executeTakeFirst();
    return account?.status === 'active' ? account : undefined;
  }

  async revoke(secret: string): Promise<void> {
    await this.db
      .updateTable('web_sessions')
      .set({ revoked_at: sql<Date>`now()` })
      .where('token_hash', '=', sha256Hex(secret))
      .execute();
  }

  async revokeAccount(accountId: string): Promise<void> {
    await this.db
      .updateTable('web_sessions')
      .set({ revoked_at: sql<Date>`now()` })
      .where('account_id', '=', accountId)
      .where('revoked_at', 'is', null)
      .execute();
  }
}
