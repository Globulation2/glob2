CREATE TABLE colony_skin_reports (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  version_id uuid NOT NULL REFERENCES colony_skin_versions(id),
  reporter_account_id uuid NOT NULL REFERENCES accounts(id),
  reason text NOT NULL CHECK (char_length(reason) BETWEEN 1 AND 1000),
  created_at timestamptz NOT NULL DEFAULT now(),
  resolution text CHECK (resolution IN ('dismissed', 'disabled')),
  resolved_at timestamptz,
  resolved_by_account_id uuid REFERENCES accounts(id),
  resolution_reason text CHECK (char_length(resolution_reason) BETWEEN 1 AND 1000),
  UNIQUE (version_id, reporter_account_id),
  CHECK ((resolution IS NULL AND resolved_at IS NULL AND resolved_by_account_id IS NULL AND resolution_reason IS NULL)
    OR (resolution IS NOT NULL AND resolved_at IS NOT NULL AND resolved_by_account_id IS NOT NULL AND resolution_reason IS NOT NULL))
);
CREATE INDEX colony_skin_reports_queue ON colony_skin_reports (created_at DESC, id DESC)
  WHERE resolution IS NULL;
