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
  type AiId,
} from '@glob2/protocol';
import { activeEntitlements } from '@glob2/worker';
import type { SigningKeys } from '../auth/keys.ts';

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

  constructor(db: Db, keys: SigningKeys, origin: string) {
    this.db = db;
    this.keys = keys;
    this.origin = origin;
  }

  /**
   * The assignment of a human player in a starting or running match, with a
   * freshly signed ticket; undefined if the account has no seat there or the
   * match is over or has no relay.
   */
  async forAccount(matchId: string, accountId: string): Promise<MatchAssignment | undefined> {
    const match = await this.db
      .selectFrom('matches as m')
      .innerJoin('relays as r', 'r.id', 'm.relay_id')
      .select(['m.id', 'm.status', 'm.setup', 'r.public_url'])
      .where('m.id', '=', matchId)
      .executeTakeFirst();
    if (!match || (match.status !== 'starting' && match.status !== 'running')) return undefined;
    const setup = match.setup as unknown as MatchSetup;
    const seat = setup.seats.find((s) => s.kind === 'human' && s.accountId === accountId);
    if (!seat) return undefined;
    const humanSeats = setup.seats.filter((s) => s.kind === 'human').map((s) => s.seat);
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
      entitlements: await activeEntitlements(this.db, accountId),
    };
    return {
      matchId: match.id,
      seat: seat.seat,
      ticket: this.keys.sign(MATCH_TICKET_TYPE, claims),
      ticketExpiresAt: new Date(claims.exp * 1000).toISOString(),
      relayUrl: match.public_url,
      setup,
      mapUrl: mapUrl(this.origin, setup.map.hash),
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

  async summary(matchId: string): Promise<MatchSummary | undefined> {
    const match = await this.db
      .selectFrom('matches')
      .selectAll()
      .where('id', '=', matchId)
      .executeTakeFirst();
    if (!match) return undefined;
    const setup = match.setup as unknown as MatchSetup;
    const participants = await this.db
      .selectFrom('match_participants')
      .selectAll()
      .where('match_id', '=', matchId)
      .orderBy('seat')
      .execute();
    const history = await this.db
      .selectFrom('rating_history as h')
      .innerJoin('match_participants as p', (join) =>
        join.onRef('p.rating_entity_id', '=', 'h.entity_id').onRef('p.match_id', '=', 'h.match_id'),
      )
      .select(['p.seat', 'h.ladder', 'h.display_before', 'h.display_after', 'h.sigma_after'])
      .where('h.match_id', '=', matchId)
      .execute();
    const ratingOf = new Map(history.map((h) => [h.seat, h]));
    return {
      id: match.id,
      simVersion: setup.simVersion,
      origin: match.origin,
      ...(match.queue_id ? { queueId: match.queue_id } : {}),
      rated: match.rated,
      status: match.status,
      verification: match.verification,
      ...(match.end_reason ? { endReason: match.end_reason } : {}),
      mapHash: match.map_hash,
      ...(match.started_at ? { startedAt: match.started_at.toISOString() } : {}),
      ...(match.ended_at ? { endedAt: match.ended_at.toISOString() } : {}),
      ...(match.final_tick !== null ? { durationTicks: match.final_tick } : {}),
      participants: participants.map((p) => {
        const rating = ratingOf.get(p.seat);
        return {
          seat: p.seat,
          team: p.team,
          kind: p.kind,
          displayName: p.display_name,
          ...(p.account_id ? { accountId: p.account_id } : {}),
          ...(p.ai_id ? { ai: p.ai_id as AiId } : {}),
          ...(p.outcome ? { outcome: p.outcome } : {}),
          disconnects: p.disconnects,
          ...(rating
            ? {
                rating: {
                  ladder: rating.ladder,
                  before: rating.display_before,
                  after: rating.display_after,
                  provisional: rating.sigma_after > 5,
                },
              }
            : {}),
        };
      }),
    };
  }
}
