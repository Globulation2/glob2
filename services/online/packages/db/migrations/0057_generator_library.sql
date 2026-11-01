CREATE TABLE generators (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name text NOT NULL,
    description text NOT NULL DEFAULT '',
    tags text[] NOT NULL DEFAULT '{}',
    visibility text NOT NULL DEFAULT 'unlisted' CHECK (visibility IN ('public', 'unlisted', 'private')),
    hidden boolean NOT NULL DEFAULT false,
    hidden_reason text,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    deleted_at timestamptz
);
CREATE INDEX generators_owner_idx ON generators(owner_account_id);
CREATE INDEX generators_tags_idx ON generators USING gin(tags);

CREATE TABLE generator_validations (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    hash text NOT NULL REFERENCES blobs(sha256),
    sim_version text NOT NULL,
    suite integer NOT NULL,
    request_hash text NOT NULL,
    example jsonb NOT NULL,
    job_id uuid REFERENCES engine_jobs(id) ON DELETE SET NULL,
    status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'valid', 'invalid', 'error')),
    report jsonb NOT NULL,
    error text,
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (hash, sim_version, suite, request_hash)
);

CREATE TABLE generator_uploads (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    validation_id uuid NOT NULL REFERENCES generator_validations(id),
    expires_at timestamptz NOT NULL DEFAULT now() + interval '1 day',
    published_generator_id uuid REFERENCES generators(id) ON DELETE SET NULL,
    published_version_id uuid
);
CREATE INDEX generator_uploads_expiry_idx ON generator_uploads(expires_at);

CREATE TABLE generator_versions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    generator_id uuid NOT NULL REFERENCES generators(id) ON DELETE CASCADE,
    hash text NOT NULL REFERENCES blobs(sha256),
    source_hash text NOT NULL REFERENCES blobs(sha256),
    package_hash text NOT NULL,
    metadata jsonb NOT NULL,
    example jsonb NOT NULL,
    revision bigint NOT NULL,
    label text NOT NULL,
    notes text NOT NULL DEFAULT '',
    profile integer NOT NULL CHECK (profile >= 1),
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (generator_id, hash),
    UNIQUE (generator_id, label),
    UNIQUE (generator_id, revision)
);
CREATE INDEX generator_versions_hash_idx ON generator_versions(hash);
ALTER TABLE generator_uploads ADD FOREIGN KEY (published_version_id) REFERENCES generator_versions(id) ON DELETE SET NULL;

CREATE TABLE generator_likes (
    generator_id uuid NOT NULL REFERENCES generators(id) ON DELETE CASCADE,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    PRIMARY KEY (generator_id, account_id)
);
CREATE TABLE generator_favourites (
    generator_id uuid NOT NULL REFERENCES generators(id) ON DELETE CASCADE,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    PRIMARY KEY (generator_id, account_id)
);
CREATE TABLE generator_downloads (
    version_id uuid NOT NULL REFERENCES generator_versions(id) ON DELETE CASCADE,
    downloader text NOT NULL,
    day date NOT NULL DEFAULT current_date,
    PRIMARY KEY (version_id, downloader, day)
);
CREATE TABLE generator_reports (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    generator_id uuid NOT NULL REFERENCES generators(id) ON DELETE CASCADE,
    reporter_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    reason text NOT NULL CHECK (reason IN ('broken', 'offensive', 'copyright', 'other')),
    details text NOT NULL,
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'resolved', 'dismissed')),
    created_at timestamptz NOT NULL DEFAULT now(),
    resolution_note text
);
CREATE UNIQUE INDEX generator_reports_open_idx ON generator_reports(generator_id, reporter_account_id) WHERE status = 'open';

-- Release source and metadata are immutable. Counters live in separate tables.
CREATE FUNCTION generator_version_immutable() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    RAISE EXCEPTION 'Published generator versions are immutable';
END;
$$;
CREATE TRIGGER generator_version_immutable BEFORE UPDATE ON generator_versions
    FOR EACH ROW EXECUTE FUNCTION generator_version_immutable();

-- Names stay reserved after deletion, including account deletion.
CREATE TABLE generator_ids (
  manifest_id text PRIMARY KEY,
  generator_id uuid REFERENCES generators(id) ON DELETE SET NULL
);

ALTER TABLE engine_jobs DROP CONSTRAINT engine_jobs_kind_check;
ALTER TABLE engine_jobs ADD CONSTRAINT engine_jobs_kind_check CHECK (kind IN ('generate-map','validate-map','render-preview','verify-match','import-ai-map','validate-ai','validate-buildings','validate-set','validate-generator','generate-script-map'));

ALTER TABLE room_members ADD COLUMN generator_support boolean NOT NULL DEFAULT false;
ALTER TABLE generated_maps ADD COLUMN chosen_seed bigint;

ALTER TABLE admin_report_resolutions DROP CONSTRAINT admin_report_resolutions_library_check;
ALTER TABLE admin_report_resolutions ADD CONSTRAINT admin_report_resolutions_library_check CHECK(library IN ('maps','ais','generators','buildings','sets','skins','music'));

ALTER TABLE map_versions ADD COLUMN generator_provenance jsonb;
