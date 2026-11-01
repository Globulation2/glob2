-- Realtime presence across API replicas. Each replica registers itself and
-- heartbeats; it records which accounts hold a realtime socket on it. A room
-- member is marked disconnected only when no live replica holds a socket for
-- the account, and the sockets of a replica that stopped heartbeating (it
-- crashed or lost the database) are expired by the others
-- (apps/api realtime/presence.ts).

CREATE TABLE api_replicas (
  id text PRIMARY KEY CHECK (char_length(id) BETWEEN 1 AND 200),
  started_at timestamptz NOT NULL DEFAULT now(),
  heartbeat_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX api_replicas_heartbeat_idx ON api_replicas (heartbeat_at);

CREATE TABLE realtime_presence (
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  replica_id text NOT NULL REFERENCES api_replicas (id) ON DELETE CASCADE,
  since timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (account_id, replica_id)
);
CREATE INDEX realtime_presence_replica_idx ON realtime_presence (replica_id);
