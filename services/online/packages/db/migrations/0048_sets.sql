ALTER TABLE engine_jobs DROP CONSTRAINT engine_jobs_kind_check;
ALTER TABLE engine_jobs ADD CONSTRAINT engine_jobs_kind_check CHECK (
 kind IN ('generate-map','validate-map','render-preview','verify-match','import-ai-map','validate-ai','validate-set')
);
CREATE TABLE asset_sets (
 id uuid PRIMARY KEY, owner_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 title text NOT NULL, description text NOT NULL DEFAULT '', tags text[] NOT NULL DEFAULT '{}',
 visibility text NOT NULL DEFAULT 'private' CHECK(visibility IN ('public','unlisted','private')),
 hidden boolean NOT NULL DEFAULT false, hidden_reason text,
 created_at timestamptz NOT NULL DEFAULT now(), updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX asset_sets_owner_idx ON asset_sets(owner_account_id);
CREATE INDEX asset_sets_tags_idx ON asset_sets USING gin(tags);
CREATE TABLE set_drafts (
 id uuid PRIMARY KEY, set_id uuid NOT NULL REFERENCES asset_sets(id) ON DELETE CASCADE,
 revision integer NOT NULL DEFAULT 0, document jsonb NOT NULL, hash text REFERENCES blobs(sha256),
 validation_job_id uuid REFERENCES engine_jobs(id) ON DELETE SET NULL,
 sim_version text, report jsonb, status text CHECK(status IN ('pending','valid','invalid','error')),
 error text, published_version_id uuid, updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE set_versions (
 id uuid PRIMARY KEY, set_id uuid NOT NULL REFERENCES asset_sets(id) ON DELETE CASCADE,
 hash text NOT NULL REFERENCES blobs(sha256), label text NOT NULL, notes text NOT NULL DEFAULT '',
 license text NOT NULL CHECK(license IN ('CC0-1.0','CC-BY-4.0')), credits jsonb NOT NULL,
 sim_version text NOT NULL, min_version_minor integer NOT NULL, report jsonb NOT NULL,
 preview_hash text REFERENCES blobs(sha256), created_at timestamptz NOT NULL DEFAULT now(),
 UNIQUE(set_id,label), UNIQUE(set_id,hash)
);
ALTER TABLE set_drafts ADD FOREIGN KEY(published_version_id) REFERENCES set_versions(id) ON DELETE SET NULL;
CREATE TABLE set_likes (
 set_id uuid NOT NULL REFERENCES asset_sets(id) ON DELETE CASCADE,
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, PRIMARY KEY(set_id,account_id)
);
CREATE TABLE set_downloads (
 version_id uuid NOT NULL REFERENCES set_versions(id) ON DELETE CASCADE,
 downloader text NOT NULL, day date NOT NULL DEFAULT current_date, PRIMARY KEY(version_id,downloader,day)
);
CREATE TABLE set_reports (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(), set_id uuid NOT NULL REFERENCES asset_sets(id) ON DELETE CASCADE,
 reporter_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 reason text NOT NULL, details text NOT NULL, resolved boolean NOT NULL DEFAULT false,
 resolution text, created_at timestamptz NOT NULL DEFAULT now()
);

ALTER TABLE map_versions ADD COLUMN set_credits jsonb NOT NULL DEFAULT '[]'::jsonb;
