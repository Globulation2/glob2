import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
export interface RateCard {
  version: string;
  model: string;
  // Integer credits per million tokens. Output includes reasoning tokens exactly once.
  input: number;
  cachedInput: number;
  cacheWrite?: number;
  output: number;
}
export interface Usage {
  input: number;
  cachedInput: number;
  cacheWrite?: number;
  output: number;
}
type Db = Kysely<Database> | Transaction<Database>;
export class HiveError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}
export function integer(value: number): number {
  if (!Number.isSafeInteger(value) || value < 0)
    throw new HiveError('bad_request', 'Invalid credit amount.');
  return value;
}
export function price(rate: RateCard, usage: Usage): number {
  for (const n of [
    rate.input,
    rate.cachedInput,
    rate.cacheWrite ?? rate.input,
    rate.output,
    usage.input,
    usage.cachedInput,
    usage.cacheWrite ?? 0,
    usage.output,
  ])
    integer(n);
  if (usage.cachedInput + (usage.cacheWrite ?? 0) > usage.input)
    throw new HiveError('bad_request', 'Invalid token accounting.');
  const numerator =
    BigInt(usage.input - usage.cachedInput - (usage.cacheWrite ?? 0)) * BigInt(rate.input) +
    BigInt(usage.cacheWrite ?? 0) * BigInt(rate.cacheWrite ?? rate.input) +
    BigInt(usage.cachedInput) * BigInt(rate.cachedInput) +
    BigInt(usage.output) * BigInt(rate.output);
  return integer(Number((numerator + 999999n) / 1000000n));
}
export type CreditProduct = 'hive' | 'maps';
export class Credits {
  readonly product: CreditProduct;
  private table(name: string) {
    return sql.table(`${this.product === 'hive' ? 'hive' : 'map'}_${name}`);
  }
  readonly db: Kysely<Database>;
  constructor(db: Kysely<Database>, product: CreditProduct = 'hive') {
    this.product = product;
    this.db = db;
  }
  private async wallet(db: Db, account: string) {
    await sql`INSERT INTO ${this.table('wallets')}(account_id) VALUES(${account}) ON CONFLICT DO NOTHING`.execute(
      db,
    );
    const row = required(
      (
        await sql<{
          balance: string;
          reserved: string;
        }>`SELECT balance,reserved FROM ${this.table('wallets')} WHERE account_id=${account} FOR UPDATE`.execute(
          db,
        )
      ).rows[0],
    );
    return { balance: Number(row.balance), reserved: Number(row.reserved) };
  }
  async balance(account: string, db: Db = this.db) {
    const row = (
      await sql<{
        balance: string;
        reserved: string;
      }>`SELECT balance,reserved FROM ${this.table('wallets')} WHERE account_id=${account}`.execute(
        db,
      )
    ).rows[0];
    return {
      balance: Number(row?.balance ?? 0),
      reserved: Number(row?.reserved ?? 0),
      available: Math.max(0, Number(row?.balance ?? 0) - Number(row?.reserved ?? 0)),
    };
  }
  async adjust(
    account: string,
    id: string,
    amount: number,
    kind: 'grant' | 'purchase' | 'refund' | 'dispute' | 'adjustment',
    details: unknown = {},
  ) {
    integer(Math.abs(amount));
    return this.db.transaction().execute(async (db) => {
      await this.wallet(db, account);
      const old = (
        await sql<{
          account_id: string;
          amount: string;
        }>`SELECT account_id,amount FROM ${this.table('ledger')} WHERE id=${id}`.execute(db)
      ).rows[0];
      if (old) {
        if (old.account_id !== account || Number(old.amount) !== amount)
          throw new HiveError('conflict', 'Credit operation changed.');
        return false;
      }
      await sql`INSERT INTO ${this.table('ledger')}(id,account_id,amount,kind,details) VALUES(${id},${account},${amount},${kind},${JSON.stringify(details)}::jsonb)`.execute(
        db,
      );
      await sql`UPDATE ${this.table('wallets')} SET balance=balance+${amount} WHERE account_id=${account}`.execute(
        db,
      );
      return true;
    });
  }
  async reserve(account: string, id: string, amount: number, rate: RateCard) {
    integer(amount);
    if (amount === 0) throw new HiveError('bad_request', 'A call needs a positive reservation.');
    price(rate, { input: 0, cachedInput: 0, output: 0 });
    return this.db.transaction().execute(async (db) => {
      const wallet = await this.wallet(db, account);
      const old = (
        await sql<{
          account_id: string;
          reserved: string;
          status: string;
          rate: RateCard;
        }>`SELECT * FROM ${this.table('calls')} WHERE id=${id}`.execute(db)
      ).rows[0];
      if (old) {
        if (
          old.account_id !== account ||
          Number(old.reserved) !== amount ||
          old.rate.version !== rate.version ||
          old.rate.model !== rate.model ||
          old.rate.input !== rate.input ||
          old.rate.cachedInput !== rate.cachedInput ||
          (old.rate.cacheWrite ?? old.rate.input) !== (rate.cacheWrite ?? rate.input) ||
          old.rate.output !== rate.output
        )
          throw new HiveError('conflict', 'Model reservation changed.');
        return false; // Never dispatch a previously journaled call again.
      }
      if (wallet.balance - wallet.reserved < amount)
        throw new HiveError(
          'credits',
          this.product === 'hive'
            ? 'Your commander needs more credits. Standing orders remain active.'
            : 'Your map studio needs more credits.',
        );
      await sql`INSERT INTO ${this.table('calls')}(id,account_id,reserved,status,rate) VALUES(${id},${account},${amount},'reserved',${JSON.stringify(rate)}::jsonb)`.execute(
        db,
      );
      await sql`UPDATE ${this.table('wallets')} SET reserved=reserved+${amount} WHERE account_id=${account}`.execute(
        db,
      );
      return true;
    });
  }
  async dispatch(id: string) {
    const rows = (
      await sql`UPDATE ${this.table('calls')} SET status='dispatched' WHERE id=${id} AND status='reserved' RETURNING id`.execute(
        this.db,
      )
    ).rows;
    return rows.length === 1;
  }
  async uncertain(id: string) {
    await sql`UPDATE ${this.table('calls')} SET status='uncertain' WHERE id=${id} AND status='dispatched'`.execute(
      this.db,
    );
  }
  async settle(account: string, id: string, usage: Usage) {
    return this.db.transaction().execute(async (db) => {
      await this.wallet(db, account);
      const call = (
        await sql<{
          account_id: string;
          reserved: string;
          status: string;
          rate: RateCard;
          charged: string | null;
          usage: Usage | null;
        }>`SELECT * FROM ${this.table('calls')} WHERE id=${id} FOR UPDATE`.execute(db)
      ).rows[0];
      if (!call || call.account_id !== account)
        throw new HiveError('not_found', 'Unknown model call.');
      const charge = price(call.rate, usage);
      if (call.status === 'settled') {
        if (
          Number(call.charged) !== charge ||
          JSON.stringify(call.usage) !== JSON.stringify(usage)
        ) {
          // jsonb key order is not significant.
          if (
            !call.usage ||
            call.usage.input !== usage.input ||
            call.usage.cachedInput !== usage.cachedInput ||
            (call.usage.cacheWrite ?? 0) !== (usage.cacheWrite ?? 0) ||
            call.usage.output !== usage.output
          )
            throw new HiveError('conflict', 'Usage settlement changed.');
        }
        return Number(call.charged);
      }
      if (charge > Number(call.reserved))
        throw new HiveError('reconcile', 'Provider usage exceeded its reservation.');
      await sql`UPDATE ${this.table('wallets')} SET balance=balance-${charge},reserved=reserved-${call.reserved} WHERE account_id=${account}`.execute(
        db,
      );
      await sql`UPDATE ${this.table('calls')} SET status='settled',charged=${charge},usage=${JSON.stringify(usage)}::jsonb WHERE id=${id}`.execute(
        db,
      );
      await sql`INSERT INTO ${this.table('ledger')}(id,account_id,amount,kind,details) VALUES(${`usage:${id}`},${account},${-charge},'usage',${JSON.stringify({ rate: call.rate, usage })}::jsonb)`.execute(
        db,
      );
      return charge;
    });
  }
}

export function required<T>(value: T | undefined | null): T {
  if (value === undefined || value === null)
    throw new HiveError('not_found', 'The commander record is unavailable.');
  return value;
}
