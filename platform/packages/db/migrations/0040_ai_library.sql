ALTER TABLE engine_jobs DROP CONSTRAINT engine_jobs_kind_check;
ALTER TABLE engine_jobs ADD CONSTRAINT engine_jobs_kind_check CHECK (
    kind IN ('generate-map', 'validate-map', 'render-preview', 'verify-match', 'import-ai-map', 'validate-ai')
);

CREATE TABLE ais (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name text NOT NULL,
    description text NOT NULL DEFAULT '',
    tags text[] NOT NULL DEFAULT '{}',
    visibility text NOT NULL DEFAULT 'public' CHECK (visibility IN ('public', 'unlisted', 'private')),
    hidden boolean NOT NULL DEFAULT false,
    hidden_reason text,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ais_owner_idx ON ais(owner_account_id);
CREATE INDEX ais_tags_idx ON ais USING gin(tags);

CREATE TABLE ai_validations (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    hash text NOT NULL REFERENCES blobs(sha256),
    sim_version text NOT NULL,
    suite integer NOT NULL,
    job_id uuid REFERENCES engine_jobs(id) ON DELETE SET NULL,
    status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'valid', 'invalid', 'error')),
    report jsonb NOT NULL,
    error text,
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (hash, sim_version, suite)
);

CREATE TABLE ai_uploads (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    validation_id uuid NOT NULL REFERENCES ai_validations(id),
    expires_at timestamptz NOT NULL DEFAULT now() + interval '1 day',
    published_ai_id uuid REFERENCES ais(id) ON DELETE SET NULL,
    published_version_id uuid
);
CREATE INDEX ai_uploads_expiry_idx ON ai_uploads(expires_at);

CREATE TABLE ai_versions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    ai_id uuid NOT NULL REFERENCES ais(id) ON DELETE CASCADE,
    hash text NOT NULL REFERENCES blobs(sha256),
    label text NOT NULL,
    notes text NOT NULL DEFAULT '',
    profile integer NOT NULL CHECK (profile IN (1, 2)),
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (ai_id, hash),
    UNIQUE (ai_id, label)
);
CREATE INDEX ai_versions_hash_idx ON ai_versions(hash);
ALTER TABLE ai_uploads ADD FOREIGN KEY (published_version_id) REFERENCES ai_versions(id) ON DELETE SET NULL;

CREATE TABLE ai_likes (
    ai_id uuid NOT NULL REFERENCES ais(id) ON DELETE CASCADE,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    PRIMARY KEY (ai_id, account_id)
);
CREATE TABLE ai_favourites (
    ai_id uuid NOT NULL REFERENCES ais(id) ON DELETE CASCADE,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    PRIMARY KEY (ai_id, account_id)
);
CREATE TABLE ai_downloads (
    version_id uuid NOT NULL REFERENCES ai_versions(id) ON DELETE CASCADE,
    downloader text NOT NULL,
    day date NOT NULL DEFAULT current_date,
    PRIMARY KEY (version_id, downloader, day)
);
CREATE TABLE ai_reports (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    ai_id uuid NOT NULL REFERENCES ais(id) ON DELETE CASCADE,
    reporter_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    reason text NOT NULL CHECK (reason IN ('broken', 'offensive', 'copyright', 'other')),
    details text NOT NULL,
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'resolved', 'dismissed')),
    created_at timestamptz NOT NULL DEFAULT now(),
    resolution_note text
);
CREATE UNIQUE INDEX ai_reports_open_idx ON ai_reports(ai_id, reporter_account_id) WHERE status = 'open';

-- Release source and metadata are immutable. Counters live in separate tables.
CREATE FUNCTION ai_version_immutable() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    RAISE EXCEPTION 'Published AI versions are immutable';
END;
$$;
CREATE TRIGGER ai_version_immutable BEFORE UPDATE ON ai_versions
    FOR EACH ROW EXECUTE FUNCTION ai_version_immutable();
