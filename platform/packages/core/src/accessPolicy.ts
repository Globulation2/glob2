// AccessPolicy: the single gate every action that admits play goes through
// (hosting a room, joining one, entering a queue). It is the paywall hook: an
// implementation could consult the `entitlements` table, but the only one that
// exists is allow-all, and no feature is paid. Product rules that are not
// about access (for example "guests cannot enter rated queues") belong to the
// feature that owns them, not here.
import type { SimVersion } from '@glob2/protocol';

export interface AccessSubject {
  accountId: string;
  kind: 'guest' | 'registered';
  role: 'user' | 'moderator' | 'admin';
  /** Active entitlement keys of the account (empty today). */
  entitlements: readonly string[];
}

export interface HostRequest {
  simVersion: SimVersion;
  visibility: 'public' | 'link';
}

export interface JoinRequest {
  roomId: string;
  hostAccountId: string;
  simVersion: SimVersion;
}

export interface QueueRequest {
  queueId: string;
  rated: boolean;
  simVersion: SimVersion;
}

export type AccessDecision =
  | { allowed: true }
  | {
      allowed: false;
      /** Human-readable reason shown to the player. */
      reason: string;
      /** Entitlement that would allow the action, when that is why it was denied. */
      requiredEntitlement?: string;
    };

export interface AccessPolicy {
  readonly name: string;
  canHost(subject: AccessSubject, request: HostRequest): Promise<AccessDecision>;
  canJoin(subject: AccessSubject, request: JoinRequest): Promise<AccessDecision>;
  canQueue(subject: AccessSubject, request: QueueRequest): Promise<AccessDecision>;
}

const ALLOWED: AccessDecision = Object.freeze({ allowed: true });

export const allowAllPolicy: AccessPolicy = Object.freeze({
  name: 'allow-all',
  canHost: async () => ALLOWED,
  canJoin: async () => ALLOWED,
  canQueue: async () => ALLOWED,
});

export class AccessDeniedError extends Error {
  readonly decision: Extract<AccessDecision, { allowed: false }>;
  constructor(decision: Extract<AccessDecision, { allowed: false }>) {
    super(decision.reason);
    this.name = 'AccessDeniedError';
    this.decision = decision;
  }
}

/** Throws AccessDeniedError (mapped to the `access_denied` error code) unless allowed. */
export function assertAllowed(decision: AccessDecision): void {
  if (!decision.allowed) throw new AccessDeniedError(decision);
}

export function createAccessPolicy(name: string): AccessPolicy {
  switch (name) {
    case 'allow-all':
      return allowAllPolicy;
    default:
      throw new Error(`unknown access policy ${name}`);
  }
}
