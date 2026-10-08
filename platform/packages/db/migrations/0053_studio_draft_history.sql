-- Private manual drafts must survive AI application, history restoration and undo.
CREATE TABLE building_studio_draft_history (
 thread_id uuid NOT NULL REFERENCES building_studio_threads(id) ON DELETE CASCADE,
 revision uuid NOT NULL, title text NOT NULL, archive bytea NOT NULL CHECK(octet_length(archive)<=33554432),
 created_at timestamptz NOT NULL DEFAULT now(), PRIMARY KEY(thread_id,revision)
);
CREATE TABLE terrain_studio_draft_history (
 thread_id uuid NOT NULL REFERENCES terrain_studio_threads(id) ON DELETE CASCADE,
 revision integer NOT NULL, document jsonb NOT NULL, hash text REFERENCES blobs(sha256),
 report jsonb, sim_version text, status text, created_at timestamptz NOT NULL DEFAULT now(),
 PRIMARY KEY(thread_id,revision)
);
