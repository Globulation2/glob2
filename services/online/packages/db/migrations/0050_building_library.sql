ALTER TABLE engine_jobs DROP CONSTRAINT engine_jobs_kind_check;
ALTER TABLE engine_jobs ADD CONSTRAINT engine_jobs_kind_check CHECK (
 kind IN ('generate-map','validate-map','render-preview','verify-match','import-ai-map','validate-ai','validate-set','validate-buildings')
);
CREATE TABLE building_families (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 owner_account_id uuid NOT NULL REFERENCES accounts(id),
 namespace uuid NOT NULL UNIQUE,
 name text NOT NULL CHECK(length(name) BETWEEN 1 AND 128),
 description text NOT NULL DEFAULT '' CHECK(length(description)<=4000),
 visibility text NOT NULL DEFAULT 'unlisted' CHECK(visibility IN ('public','unlisted','private')),
 hidden boolean NOT NULL DEFAULT false,
 download_count integer NOT NULL DEFAULT 0 CHECK(download_count>=0),
 hidden_reason text,
 created_at timestamptz NOT NULL DEFAULT now(),
 updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE building_releases (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 family_id uuid NOT NULL REFERENCES building_families(id) ON DELETE CASCADE,
 archive_hash text NOT NULL REFERENCES blobs(sha256),
 job_id uuid NOT NULL UNIQUE REFERENCES engine_jobs(id),
 sim_version text NOT NULL,
 base_hash text NOT NULL CHECK(base_hash ~ '^[0-9a-f]{64}$'),
 suite integer NOT NULL CHECK(suite=1),
 created_at timestamptz NOT NULL DEFAULT now(),
 UNIQUE(family_id,archive_hash,sim_version,base_hash,suite)
);
CREATE INDEX building_releases_family_idx ON building_releases(family_id,created_at DESC);
CREATE TABLE building_likes (
 family_id uuid NOT NULL REFERENCES building_families(id) ON DELETE CASCADE,
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 PRIMARY KEY(family_id,account_id)
);
CREATE TABLE building_favourites (LIKE building_likes INCLUDING ALL);
ALTER TABLE building_favourites ADD FOREIGN KEY(family_id) REFERENCES building_families(id) ON DELETE CASCADE;
ALTER TABLE building_favourites ADD FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE;
CREATE TABLE building_reports (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 family_id uuid NOT NULL REFERENCES building_families(id) ON DELETE CASCADE,
 reporter_account_id uuid REFERENCES accounts(id) ON DELETE SET NULL,
 reason text NOT NULL CHECK(length(reason) BETWEEN 1 AND 2000),
 resolved boolean NOT NULL DEFAULT false,
 created_at timestamptz NOT NULL DEFAULT now()
);
