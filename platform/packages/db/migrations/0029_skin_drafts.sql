-- One bounded, private working canvas per account; publishing remains separate.
CREATE TABLE colony_skin_drafts (
  account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
  revision uuid NOT NULL DEFAULT gen_random_uuid(),
  name text NOT NULL CHECK (char_length(name) BETWEEN 1 AND 64),
  building_color integer NOT NULL CHECK (building_color BETWEEN 0 AND 16777215),
  image bytea NOT NULL CHECK (octet_length(image) BETWEEN 1 AND 262144),
  updated_at timestamptz NOT NULL DEFAULT now()
);
