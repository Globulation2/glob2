-- Authored families stay private until engine validation and publication.
CREATE TABLE building_drafts (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    revision uuid NOT NULL DEFAULT gen_random_uuid(),
    name text NOT NULL CHECK (length(name) BETWEEN 1 AND 128),
    archive bytea NOT NULL CHECK (octet_length(archive) <= 33554432),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX building_drafts_owner_idx ON building_drafts(owner_account_id, updated_at DESC);
