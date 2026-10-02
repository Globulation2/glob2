// Realtime side of rooms, queues and matches: the room.*, queue.* and
// match.reconnect methods, and delivery of everything other processes
// publish (room changes, chat, match starts, queue events from the worker's
// matchmaker, match updates from ratings and intake, finished map jobs).
import type { Logger } from '@glob2/core';
import { resolveQueue, type AccessPolicy, type PlatformConfig } from '@glob2/core';
import type { PgPubSub } from '@glob2/db';
import {
  simVersionKey,
  type RealtimeMethod,
  type RealtimeParams,
  type SimVersion,
} from '@glob2/protocol';
import {
  MAP_JOBS_CHANNEL,
  MATCH_UPDATES_CHANNEL,
  QUEUE_EVENTS_CHANNEL,
  accessSubject,
  joinQueue,
  leaveQueue,
  moveMatchToAnotherRelay,
  respondToProposal,
  type PlayFanout,
  type QueueNotification,
  sendProposal,
  updateTicket,
  PgQueueNotifier,
} from '@glob2/worker';
import { apiError } from '../errors.ts';

const queueNotifier = new PgQueueNotifier();
import { WindowCounter } from '../http/validate.ts';
import type { RealtimeConnection, MethodHandler } from '../realtime/connection.ts';
import type { RealtimeHub } from '../realtime/hub.ts';
import type { Assignments } from './assignments.ts';
import type { RoomService } from './rooms.ts';

export interface PlayRealtimeOptions {
  config: PlatformConfig;
  access: AccessPolicy;
  pubsub: PgPubSub;
  hub: RealtimeHub;
  rooms: RoomService;
  assignments: Assignments;
  logger: Logger;
  /** Room sweep interval (default 30 s); 0 disables it. */
  sweepMs?: number;
}

/** Failed invite-code lookups allowed per account and per address, per 10 minutes. */
export const CODE_FAILURES_PER_WINDOW = 10;
const CODE_WINDOW_MS = 10 * 60_000;
/** Chat messages per account per 10 seconds. */
const CHAT_PER_WINDOW = 8;

export class PlayRealtime {
  readonly handlers: Partial<Record<RealtimeMethod, MethodHandler>>;
  private readonly options: PlayRealtimeOptions;
  private readonly codeFailures = new WindowCounter(CODE_FAILURES_PER_WINDOW, CODE_WINDOW_MS);
  private readonly chatLimit = new WindowCounter(CHAT_PER_WINDOW, 10_000);
  private readonly unsubscribe: (() => Promise<void>)[] = [];
  private sweepTimer: NodeJS.Timeout | undefined;

  constructor(options: PlayRealtimeOptions) {
    this.options = options;
    this.handlers = this.buildHandlers();
  }

  async start(): Promise<void> {
    const { hub, pubsub, rooms, logger } = this.options;
    hub.onPlay = (message) => this.background(this.deliver(message), 'play delivery');
    hub.onAccountHere = (accountId) =>
      this.background(rooms.markConnected(accountId), 'room presence');
    hub.onAccountGone = (accountId) =>
      this.background(rooms.markDisconnected(accountId), 'room presence');
    this.unsubscribe.push(
      await pubsub.subscribe(QUEUE_EVENTS_CHANNEL, (payload) =>
        this.background(this.queueEvent(payload as QueueNotification), 'queue event'),
      ),
      await pubsub.subscribe(MATCH_UPDATES_CHANNEL, (payload) =>
        this.background(
          this.matchUpdated((payload as { matchId: string }).matchId),
          'match update',
        ),
      ),
      await pubsub.subscribe(MAP_JOBS_CHANNEL, () =>
        this.background(rooms.refreshPendingMaps(), 'room map refresh'),
      ),
    );
    const sweepMs = this.options.sweepMs ?? 30_000;
    if (sweepMs > 0) {
      this.sweepTimer = setInterval(
        () =>
          this.background(
            rooms.sweep().then(() => undefined),
            'room sweep',
          ),
        sweepMs,
      );
      this.sweepTimer.unref();
    }
    logger.debug('room and match delivery ready');
  }

  async stop(): Promise<void> {
    clearInterval(this.sweepTimer);
    for (const off of this.unsubscribe.splice(0)) await off();
  }

  private background(work: Promise<unknown>, what: string): void {
    work.catch((error: unknown) => this.options.logger.error({ err: error }, `${what} failed`));
  }

  // -------------------------------------------------------------- delivery

  private send(
    accountId: string,
    event: Parameters<RealtimeConnection['sendEvent']>[0],
    data: object,
  ) {
    for (const connection of this.options.hub.connectionsOf(accountId)) {
      connection.sendEvent(event, data);
    }
  }

  private local(accountIds: Iterable<string>): string[] {
    return [...accountIds].filter((id) => this.options.hub.connectionsOf(id).length > 0);
  }

  async deliver(message: PlayFanout): Promise<void> {
    const { rooms, assignments } = this.options;
    switch (message.t) {
      case 'room': {
        const state = await rooms.state(message.roomId);
        if (!state || state.status === 'closed') return;
        for (const member of state.members) {
          if (this.options.hub.connectionsOf(member.accountId).length === 0) continue;
          this.send(member.accountId, 'room.state', { room: state });
          // A socket here means the member is connected, whatever another
          // replica concluded when one of its sockets closed.
          if (!member.connected) await rooms.markConnected(member.accountId);
        }
        return;
      }
      case 'roomChat': {
        const members = this.local(await rooms.memberIds(message.roomId));
        if (members.length === 0) return;
        const chat = await rooms.chatMessage(message.messageId);
        if (!chat) return;
        for (const accountId of members) this.send(accountId, 'room.chat', { message: chat });
        return;
      }
      case 'roomClosed': {
        for (const accountId of this.local(message.accountIds)) {
          this.send(accountId, 'room.closed', { roomId: message.roomId, reason: message.reason });
        }
        return;
      }
      case 'matchStart': {
        const accounts = this.local(
          message.accountIds ?? (await assignments.humanAccounts(message.matchId)),
        );
        for (const accountId of accounts) await this.sendMatchStart(message.matchId, accountId);
        return;
      }
    }
  }

  private async sendMatchStart(matchId: string, accountId: string): Promise<void> {
    const assignment = await this.options.assignments.forAccount(matchId, accountId);
    if (assignment) this.send(accountId, 'match.start', assignment);
  }

  private async queueEvent(notification: QueueNotification): Promise<void> {
    if (this.options.hub.connectionsOf(notification.accountId).length === 0) return;
    this.send(notification.accountId, notification.event, notification.data);
    // queue.matchFound promises match.start: the matchmaker sends it right
    // after the starter placed the match, so the ticket follows it here.
    if (notification.event === 'queue.matchFound') {
      const data = notification.data as { matchId: string };
      await this.sendMatchStart(data.matchId, notification.accountId);
    }
  }

  private async matchUpdated(matchId: string): Promise<void> {
    const accounts = this.local(await this.options.assignments.humanAccounts(matchId));
    if (accounts.length === 0) return;
    const match = await this.options.assignments.summary(matchId);
    if (!match) return;
    for (const accountId of accounts) this.send(accountId, 'match.updated', { match });
  }

  // --------------------------------------------------------------- methods

  /** The socket's sim version, or update_required when this instance does not serve it. */
  private requireSim(connection: RealtimeConnection): SimVersion {
    if (!connection.simVersion || !connection.simSupported) {
      throw apiError(
        'update_required',
        'This game version is not supported here; update the game.',
      );
    }
    return connection.simVersion;
  }

  private buildHandlers(): Partial<Record<RealtimeMethod, MethodHandler>> {
    const { rooms, assignments, config, access } = this.options;
    return {
      'room.create': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.create'>;
        const account = connection.requireAccount();
        const room = await rooms.create(account, this.requireSim(connection), params);
        return { room };
      },

      'room.join': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.join'>;
        const account = connection.requireAccount();
        const sim = this.requireSim(connection);
        const keys = [`a:${account.id}`, `ip:${connection.ip}`];
        if (keys.some((key) => this.codeFailures.exhausted(key))) {
          throw apiError('rate_limited', 'Too many unknown invite codes; try again later.');
        }
        try {
          return { room: await rooms.join(account, sim, params.code, params.regions) };
        } catch (error) {
          if ((error as { body?: { code?: string } }).body?.code === 'not_found') {
            for (const key of keys) this.codeFailures.take(key);
          }
          throw error;
        }
      },

      'room.leave': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.leave'>;
        await rooms.leave(connection.requireAccount().id, params.roomId);
        return {};
      },

      'room.update': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.update'>;
        const room = await rooms.update(
          connection.requireAccount(),
          params.roomId,
          params.revision,
          params.changes,
        );
        return { room };
      },

      'room.setSeat': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.setSeat'>;
        const room = await rooms.setSeat(
          connection.requireAccount(),
          params.roomId,
          params.seat,
          params.occupant,
        );
        return { room };
      },

      'room.kick': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.kick'>;
        return {
          room: await rooms.kick(connection.requireAccount(), params.roomId, params.accountId),
        };
      },

      'room.setReady': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.setReady'>;
        return {
          room: await rooms.setReady(connection.requireAccount(), params.roomId, params.ready),
        };
      },

      'room.chat': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.chat'>;
        const account = connection.requireAccount();
        if (!this.chatLimit.take(account.id)) {
          throw apiError('rate_limited', 'You are sending messages too quickly.');
        }
        return { message: await rooms.chat(account, params.roomId, params.text) };
      },

      'room.start': async (connection, raw) => {
        const params = raw as RealtimeParams<'room.start'>;
        return rooms.start(connection.requireAccount(), params.roomId);
      },

      'queue.join': async (connection, raw) => {
        const params = raw as RealtimeParams<'queue.join'>;
        const account = connection.requireAccount();
        const sim = this.requireSim(connection);
        const configured = config.instance.queues.find((q) => q.id === params.queueId);
        if (!configured) throw apiError('not_found', `No queue ${params.queueId}.`);
        const queue = resolveQueue(configured);
        const decision = await access.canQueue(await this.subject(account.id), {
          queueId: queue.id,
          rated: queue.rated,
          simVersion: sim,
        });
        if (!decision.allowed) {
          throw apiError(
            'access_denied',
            decision.reason,
            decision.requiredEntitlement
              ? { requiredEntitlement: decision.requiredEntitlement }
              : undefined,
          );
        }
        const result = await joinQueue(this.db, {
          accountId: account.id,
          queue,
          simVersion: simVersionKey(sim),
          regions: params.regions,
          ...(params.allowAiOpponent === undefined
            ? {}
            : { allowAiOpponent: params.allowAiOpponent }),
        });
        if (result.ok)
          return { ticketId: result.ticketId, joinedAt: result.joinedAt.toISOString() };
        switch (result.code) {
          case 'guest_not_allowed':
            throw apiError('forbidden', 'Rated queues are for registered accounts; sign in first.');
          case 'already_queued':
            throw apiError('conflict', 'You are already in a queue.');
          case 'account_inactive':
            throw apiError('forbidden', 'Your account cannot play.');
          case 'cooldown':
            throw apiError('rate_limited', 'You declined a match; wait before queueing again.', {
              until: result.until.toISOString(),
            });
        }
        throw apiError('internal', 'Unknown queue result.');
      },

      'queue.leave': async (connection, raw) => {
        const params = raw as RealtimeParams<'queue.leave'>;
        const outcome = await leaveQueue(this.db, connection.requireAccount().id, params.ticketId);
        if (outcome === 'not_found') throw apiError('not_found', 'No such queue ticket.');
        if (outcome === 'starting') throw apiError('conflict', 'Your match is already starting.');
        return {};
      },

      'queue.respond': async (connection, raw) => {
        const params = raw as RealtimeParams<'queue.respond'>;
        const outcome = await respondToProposal(
          this.db,
          connection.requireAccount().id,
          params.proposalId,
          params.accept,
        );
        if (outcome === 'not_found') throw apiError('not_found', 'No such proposal.');
        if (outcome === 'not_pending') throw apiError('conflict', 'The proposal is over.');
        // Everyone in the prompt sees who has answered.
        if (outcome === 'recorded') await sendProposal(this.db, queueNotifier, params.proposalId);
        return {};
      },

      'queue.update': async (connection, raw) => {
        const params = raw as RealtimeParams<'queue.update'>;
        const outcome = await updateTicket(
          this.db,
          connection.requireAccount().id,
          params.ticketId,
          params.allowAiOpponent,
        );
        if (outcome === 'not_found') throw apiError('not_found', 'No such queue ticket.');
        if (outcome === 'not_waiting')
          throw apiError('conflict', 'The ticket is no longer waiting.');
        return {};
      },

      'match.reconnect': async (connection, raw) => {
        const params = raw as RealtimeParams<'match.reconnect'>;
        const account = connection.requireAccount();
        const seated = await assignments.forAccount(params.matchId, account.id);
        if (!seated)
          throw apiError('not_found', 'You have no seat in a running match with that id.');
        if (params.relayUnavailable) {
          const moved = await moveMatchToAnotherRelay(this.db, params.matchId);
          if (!moved.moved && moved.reason === 'no_relay') {
            throw apiError('unavailable', 'No other relay is available; try again shortly.');
          }
          if (!moved.moved && moved.reason === 'too_many_attempts') {
            throw apiError('unavailable', 'No relay accepted the match.');
          }
          if (moved.moved) {
            const fresh = await assignments.forAccount(params.matchId, account.id);
            if (fresh) return fresh;
          }
        }
        return seated;
      },
    };
  }

  private get db() {
    return this.options.rooms.database;
  }

  private async subject(accountId: string) {
    const subject = await accessSubject(this.db, accountId);
    if (!subject) throw apiError('forbidden', 'Your account cannot play.');
    return subject;
  }
}
