-- Account deletion (self-service and moderation): scrub the player's names
-- from what the platform keeps, keeping ids so history stays consistent.
-- See docs/multiplayer/identity.md, "Deleting an account".

-- A MatchSetup with the account's seats renamed "Deleted player".
CREATE FUNCTION scrub_match_setup(p_setup jsonb, p_account text) RETURNS jsonb
LANGUAGE sql IMMUTABLE AS $$
  SELECT CASE
    WHEN jsonb_typeof(p_setup -> 'seats') <> 'array' THEN p_setup
    ELSE jsonb_set(p_setup, '{seats}', (
      SELECT coalesce(jsonb_agg(
               CASE WHEN s ->> 'accountId' = p_account
                    THEN jsonb_set(s, '{name}', '"Deleted player"') ELSE s END
               ORDER BY i), '[]'::jsonb)
      FROM jsonb_array_elements(p_setup -> 'seats') WITH ORDINALITY AS e(s, i)))
  END
$$;

-- A case-insensitive whole-word pattern matching any of the names.
CREATE FUNCTION name_pattern(p_names text[]) RETURNS text
LANGUAGE sql IMMUTABLE AS $$
  SELECT '\m(' || string_agg(regexp_replace(n, '([.^$*+?()\[\]{}|\\-])', '\\\1', 'g'), '|') || ')\M'
  FROM unnest(p_names) AS n
  WHERE char_length(n) > 0
$$;

-- The audit log is append-only for the services; this is the one change
-- allowed to it: in entries about the account, every name of it in a text
-- value becomes "Deleted player" (ids, actions and times stay). Runs as its
-- owner (the migrator).
CREATE FUNCTION scrub_audit_log_account(p_account uuid, p_names text[]) RETURNS integer
LANGUAGE plpgsql SECURITY DEFINER SET search_path = public, pg_temp AS $$
DECLARE
  pattern text := name_pattern(p_names);
  changed integer;
BEGIN
  IF pattern IS NULL THEN
    RETURN 0;
  END IF;
  UPDATE admin_audit_log a
  SET details = (
    SELECT coalesce(jsonb_object_agg(d.k,
             CASE WHEN jsonb_typeof(d.v) = 'string'
                  THEN to_jsonb(regexp_replace(d.v #>> '{}', pattern, 'Deleted player', 'gi'))
                  ELSE d.v END), '{}'::jsonb)
    FROM jsonb_each(a.details) AS d(k, v)
  ) || '{"scrubbed": true}'::jsonb
  WHERE a.target_type = 'account' AND a.target_id = p_account::text;
  GET DIAGNOSTICS changed = ROW_COUNT;
  RETURN changed;
END
$$;
REVOKE ALL ON FUNCTION scrub_audit_log_account(uuid, text[]) FROM PUBLIC;

-- Matches of a deleted account that were still running or awaiting
-- verification when it was deleted: their setup (which the verifier replays)
-- is scrubbed by the worker once they are settled.
CREATE TABLE account_name_scrubs (
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  match_id uuid NOT NULL REFERENCES matches (id) ON DELETE CASCADE,
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (account_id, match_id)
);

ALTER TABLE accounts ADD COLUMN deleted_at timestamptz;
