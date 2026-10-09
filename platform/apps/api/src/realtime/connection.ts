// One realtime socket: envelope parsing, request/response correlation, a
// per-socket token bucket, heartbeat bookkeeping and the socket's session
// state (hello, account, sign-in family).
import { randomUUID } from 'node:crypto';
import type { WebSocket } from 'ws';
import type { Account } from '@glob2/db';
import type { Logger } from '@glob2/core';
import {
  RealtimeRequest,
  realtimeMethods,
  schemaIssues,
  type ClientPlatform,
  type ErrorBody,
  type RealtimeEventName,
  type RealtimeMethod,
  type SimVersion,
} from '@glob2/protocol';
import { HttpError, apiError } from '../errors.ts';

function scriptedContract(data: unknown): boolean {
  if (!data || typeof data !== 'object') return false;
  const value = data as { room?: { map?: { kind?: string } }; setup?: { map?: { kind?: string } } };
  return value.room?.map?.kind === 'scripted' || value.setup?.map?.kind === 'scripted';
}

export interface RateLimit {
  perSecond: number;
  burst: number;
}

/** Messages beyond the rate limit before the socket is closed. */
const MAX_REJECTED = 50;

export type MethodHandler = (
  connection: RealtimeConnection,
  params: Record<string, unknown>,
) => Promise<object>;

export class RealtimeConnection {
  readonly id = randomUUID();
  readonly socket: WebSocket;
  readonly ip: string;
  private readonly logger: Logger;
  private readonly limit: RateLimit;
  private tokens: number;
  private refilledAt = Date.now();
  private rejected = 0;

  helloDone = false;
  generatorSharing = false;
  platform: ClientPlatform = 'desktop';
  simVersion: SimVersion | undefined;
  simSupported = false;
  account: Account | undefined;
  /** Refresh-token family of the access token the socket authenticated with. */
  familyId: string | undefined;
  readonly pendingAttempts = new Set<string>();
  onAccountChange: ((connection: RealtimeConnection) => void) | undefined;
  onActivity: ((account: Account) => Promise<void>) | undefined;

  constructor(socket: WebSocket, ip: string, logger: Logger, limit: RateLimit) {
    this.socket = socket;
    this.ip = ip;
    this.logger = logger;
    this.limit = limit;
    this.tokens = limit.burst;
  }

  get open(): boolean {
    return this.socket.readyState === this.socket.OPEN;
  }

  send(frame: object): void {
    if (this.open) this.socket.send(JSON.stringify(frame));
  }

  sendEvent(event: RealtimeEventName, data: object): void {
    if (!this.generatorSharing && scriptedContract(data)) {
      this.close(4000, 'Update required: shared generator support');
      return;
    }
    this.send({ type: 'event', event, data });
  }

  close(code: number, reason: string): void {
    if (this.open) this.socket.close(code, reason.slice(0, 120));
  }

  authenticate(account: Account, familyId: string): void {
    this.account = account;
    this.familyId = familyId;
    this.onAccountChange?.(this);
  }

  signOut(): void {
    this.account = undefined;
    this.familyId = undefined;
    this.onAccountChange?.(this);
  }

  /** The socket's account, or an unauthenticated error. */
  requireAccount(): Account {
    if (!this.account) throw apiError('unauthenticated', 'Sign in first (session.authenticate).');
    return this.account;
  }

  private takeToken(): boolean {
    const now = Date.now();
    this.tokens = Math.min(
      this.limit.burst,
      this.tokens + ((now - this.refilledAt) / 1000) * this.limit.perSecond,
    );
    this.refilledAt = now;
    if (this.tokens < 1) return false;
    this.tokens -= 1;
    return true;
  }

  private fail(id: string, error: ErrorBody): void {
    this.send({ type: 'response', id, ok: false, error });
  }

  /** Handles one text frame. */
  async receive(
    raw: string,
    handlers: Partial<Record<RealtimeMethod, MethodHandler>>,
  ): Promise<void> {
    let frame: unknown;
    try {
      frame = JSON.parse(raw);
    } catch {
      this.close(1007, 'frames must be JSON');
      return;
    }
    const id =
      typeof frame === 'object' &&
      frame !== null &&
      typeof (frame as { id?: unknown }).id === 'string'
        ? (frame as { id: string }).id.slice(0, 64) || '?'
        : undefined;
    if (!this.takeToken()) {
      if (++this.rejected > MAX_REJECTED) {
        this.close(1008, 'rate limit exceeded');
        return;
      }
      if (id) this.fail(id, { code: 'rate_limited', message: 'Too many messages; slow down.' });
      return;
    }
    const envelopeIssues = schemaIssues(RealtimeRequest, frame);
    if (envelopeIssues.length > 0) {
      if (!id) {
        this.close(1008, 'invalid request envelope');
        return;
      }
      this.fail(id, {
        code: 'bad_request',
        message: 'Invalid request envelope.',
        details: envelopeIssues,
      });
      return;
    }
    const request = frame as RealtimeRequest;
    const contract = (realtimeMethods as Record<string, (typeof realtimeMethods)[RealtimeMethod]>)[
      request.method
    ];
    if (!contract) {
      this.fail(request.id, { code: 'bad_request', message: `Unknown method ${request.method}.` });
      return;
    }
    const paramIssues = schemaIssues(contract.params, request.params);
    if (paramIssues.length > 0) {
      this.fail(request.id, {
        code: 'bad_request',
        message: `Invalid params for ${request.method}.`,
        details: paramIssues,
      });
      return;
    }
    if (!this.helloDone && request.method !== 'session.hello') {
      this.fail(request.id, { code: 'bad_request', message: 'Send session.hello first.' });
      return;
    }
    const handler = handlers[request.method as RealtimeMethod];
    if (!handler) {
      this.fail(request.id, {
        code: 'unsupported',
        message: `${request.method} is not available on this server yet.`,
      });
      return;
    }
    try {
      const result = await handler(this, request.params as Record<string, unknown>);
      if (!this.generatorSharing && scriptedContract(result))
        throw apiError(
          'update_required',
          'Update the game to use shared generator rooms and matches.',
        );
      if (this.account) await this.onActivity?.(this.account);
      this.send({ type: 'response', id: request.id, ok: true, result });
    } catch (error) {
      if (error instanceof HttpError) {
        this.fail(request.id, error.body);
      } else {
        this.logger.error({ err: error, method: request.method }, 'realtime request failed');
        this.fail(request.id, { code: 'internal', message: 'Internal server error.' });
      }
    }
  }
}
