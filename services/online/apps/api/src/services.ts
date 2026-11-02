import type { Kysely } from 'kysely';
import type { AccessPolicy, BlobStore, JobQueue, Logger, PlatformConfig } from '@glob2/core';
import type { Database, PgPubSub } from '@glob2/db';
import type { SigningKeys } from './auth/keys.ts';

/** Everything the API needs from outside, injected so tests can supply their own. */
export interface ApiServices {
  config: PlatformConfig;
  logger: Logger;
  db: Kysely<Database>;
  pubsub: PgPubSub;
  jobs: JobQueue;
  blobs: BlobStore;
  access: AccessPolicy;
  /** Signing keys; loaded from the configuration when omitted. */
  keys?: SigningKeys;
}
