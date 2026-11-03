CREATE TABLE music_releases (
 id uuid PRIMARY KEY,
 owner_id uuid NOT NULL REFERENCES accounts(id),
 metadata jsonb NOT NULL,
 status text NOT NULL DEFAULT 'draft' CHECK(status IN ('draft','inspecting','inspected','converting','ready','published','withdrawn','failed')),
 sources jsonb NOT NULL DEFAULT '{}',
 inspection jsonb,
 result jsonb,
 options jsonb,
 error text,
 hidden boolean NOT NULL DEFAULT false,
 downloads integer NOT NULL DEFAULT 0 CHECK(downloads >= 0),
 created_at timestamptz NOT NULL DEFAULT now(),
 updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX music_owner ON music_releases(owner_id, created_at DESC);
CREATE INDEX music_public ON music_releases(created_at DESC, id) WHERE status='published' AND NOT hidden;
CREATE TABLE music_assets (
 release_id uuid NOT NULL REFERENCES music_releases(id) ON DELETE CASCADE,
 kind text NOT NULL CHECK(kind IN ('calm','building','combat','cover','zip','waveforms')),
 sha256 text NOT NULL REFERENCES blobs(sha256),
 PRIMARY KEY(release_id,kind)
);
CREATE TABLE music_likes (
 release_id uuid NOT NULL REFERENCES music_releases(id) ON DELETE CASCADE,
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 PRIMARY KEY(release_id,account_id)
);
CREATE TABLE music_reports (
 id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
 release_id uuid NOT NULL REFERENCES music_releases(id) ON DELETE CASCADE,
 account_id uuid NOT NULL REFERENCES accounts(id),
 reason text NOT NULL CHECK(char_length(reason) BETWEEN 1 AND 2000),
 resolved boolean NOT NULL DEFAULT false,
 created_at timestamptz NOT NULL DEFAULT now()
);
