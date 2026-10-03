import { matchColonySkins } from '../skins/matches.ts';
// Match tickets and match.start payloads. Tickets are signed by the replica
// that delivers them, with the same Ed25519 key (and JWKS) as access tokens,
// at the moment of delivery, so each player gets a full lifetime.
import { randomUUID } from 'node:crypto';
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  MATCH_TICKET_AUDIENCE,
  MATCH_TICKET_TYPE,
  type MatchAssignment,
  type MatchSetup,
  type MatchSummary,
  type MatchTicketClaims,
} from '@glob2/protocol';
import {
  STORED_MATCH_SETUP,
  activeEntitlements,
  matchRatingPreview,
  readStored,
} from '@glob2/play';
import type { SigningKeys } from '../auth/keys.ts';
import {
  catalogTitles,
  generatorLabel,
  generatorOf,
  summarize,
  uploadTitle,
} from '../history/summaries.ts';

type Db = Kysely<Database>;

/** Ticket lifetime. A player who needs a new one (reconnect later) calls match.reconnect. */
export const TICKET_SECONDS = 15 * 60;

export function mapUrl(origin: string, hash: string): string {
  return `${origin}/api/v1/blobs/maps/${hash}`;
}

export class Assignments {
  private readonly db: Db;
  private readonly keys: SigningKeys;
  private readonly origin: string;
  private readonly queueNames: ReadonlyMap<string, string>;

  constructor(
    db: Db,
    keys: SigningKeys,
    origin: string,
    queueNames: ReadonlyMap<string, string> = new Map(),
  ) {
    this.db = db;
    this.keys = keys;
    this.origin = origin;
    this.queueNames = queueNames;
  }

  /**
   * The assignment of a human player in a starting or running match, with a
   * freshly signed ticket; undefined if the account has no seat there or the
   * match is over or has no relay. Carries the map title, and for rated
   * matches the player's rating preview.
   */
  async forAccount(matchId: string, accountId: string): Promise<MatchAssignment | undefined> {
    const match = await this.db
      .selectFrom('matches as m')
      .innerJoin('relays as r', 'r.id', 'm.relay_id')
      .select(['m.id', 'm.status', 'm.setup', 'r.id as relay_id', 'r.region', 'r.public_url'])
      .where('m.id', '=', matchId)
      .executeTakeFirst();
    if (!match || (match.status !== 'starting' && match.status !== 'running')) return undefined;
    const setup = readStored(STORED_MATCH_SETUP, match.setup);
    const seat = setup.seats.find((s) => s.kind === 'human' && s.accountId === accountId);
    if (!seat) return undefined;
    const humanSeats = setup.seats.filter((s) => s.kind === 'human').map((s) => s.seat);
    const [title, preview, entitlements, colonySkins] = await Promise.all([
      this.mapTitle(setup),
      matchRatingPreview(this.db, match.id, accountId),
      activeEntitlements(this.db, accountId),
      matchColonySkins(this.db, this.keys, this.origin, matchId),
    ]);
    // Appearance freezing can wait for another replica. Do not deliver a relay
    // assignment read before a concurrent failover completed.
    const current = await this.db
      .selectFrom('matches')
      .select(['relay_id', 'status'])
      .where('id', '=', matchId)
      .executeTakeFirst();
    if (!current || !['starting', 'running'].includes(current.status)) return undefined;
    if (current.relay_id !== match.relay_id) return this.forAccount(matchId, accountId);
    const iat = Math.floor(Date.now() / 1000);
    const claims: MatchTicketClaims = {
      iss: this.origin,
      aud: MATCH_TICKET_AUDIENCE,
      sub: accountId,
      jti: randomUUID(),
      iat,
      exp: iat + TICKET_SECONDS,
      matchId: match.id,
      seat: seat.seat,
      accountId,
      simVersion: setup.simVersion,
      humanSeats,
      // Verbatim: the relay accepts only tickets naming its registered publicUrl.
      relayUrl: match.public_url,
      entitlements,
    };
    return {
      matchId: match.id,
      seat: seat.seat,
      ticket: this.keys.sign(MATCH_TICKET_TYPE, claims),
      ticketExpiresAt: new Date(claims.exp * 1000).toISOString(),
      relayUrl: match.public_url,
      relayId: match.relay_id,
      ...(match.region ? { relayRegion: match.region } : {}),
      setup,
      colonySkins,
      mapUrl: mapUrl(this.origin, setup.map.hash),
      ...(title ? { mapTitle: title } : {}),
      ...(preview ? { ratingPreview: preview } : {}),
    };
  }

  /** Human participants of a match (whom match.start and match.updated go to). */
  async humanAccounts(matchId: string): Promise<string[]> {
    const rows = await this.db
      .selectFrom('match_participants')
      .select('account_id')
      .where('match_id', '=', matchId)
      .where('kind', '=', 'human')
      .where('account_id', 'is not', null)
      .execute();
    return rows.flatMap((r) => (r.account_id ? [r.account_id] : []));
  }

  /** The match's summary as history lists show it (match.updated), map title included. */
  async summary(matchId: string): Promise<MatchSummary | undefined> {
    const match = await this.db
      .selectFrom('matches')
      .selectAll()
      .where('id', '=', matchId)
      .executeTakeFirst();
    if (!match) return undefined;
    return (await summarize(this.db, [match], this.queueNames))[0];
  }

  /** The catalog title of the map, else the name of the generator that made it. */
  private async mapTitle(setup: MatchSetup): Promise<string | undefined> {
    const title = (await catalogTitles(this.db, [setup.map.hash])).get(setup.map.hash)?.title;
    if (title) return title.slice(0, 128);
    const generator = generatorOf(setup);
    if (generator) return generatorLabel(generator).slice(0, 128);
    // A premade map the host uploaded for the room.
    return uploadTitle(this.db, setup.map.hash);
  }
}
