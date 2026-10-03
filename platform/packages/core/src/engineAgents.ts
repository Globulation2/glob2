// Engine agent liveness: the one definition of "an agent is fresh", shared by
// the API (which sim versions this instance can serve) and the worker (which
// sim versions the warm map pool fills).
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { EngineJobKind } from '@glob2/protocol';

/** Engine agents update engine_agents.last_seen_at this often (apps/engine-agent main.ts). */
export const ENGINE_AGENT_HEARTBEAT_SECONDS = 60;

/** An agent seen this recently (five missed heartbeats) counts as serving its sim version. */
export const ENGINE_AGENT_FRESH_SECONDS = 5 * ENGINE_AGENT_HEARTBEAT_SECONDS;

/** Sim version keys with a fresh engine agent, optionally one that runs `kind` jobs. */
export async function freshAgentSimVersions(
  db: Kysely<Database>,
  options: { kind?: EngineJobKind; freshSeconds?: number } = {},
): Promise<string[]> {
  const freshSeconds = options.freshSeconds ?? ENGINE_AGENT_FRESH_SECONDS;
  const rows = await db
    .selectFrom('engine_agents')
    .select('sim_version')
    .distinct()
    .where('last_seen_at', '>', sql<Date>`now() - make_interval(secs => ${freshSeconds})`)
    .$if(options.kind !== undefined, (qb) =>
      qb.where(sql<boolean>`${options.kind ?? ''} = ANY(kinds)`),
    )
    .orderBy('sim_version')
    .execute();
  return rows.map((r) => r.sim_version);
}
