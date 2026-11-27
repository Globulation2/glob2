-- Problem and crash reports from game clients (docs/multiplayer/problem-reports.md).
-- One row per accepted upload: the context fields the client sent and the
-- attachment list of its bundle. The bundle itself is in the blob store under
-- bundle_key and is deleted after 90 days (bundle_key becomes NULL); the row
-- is deleted after two years. No account, name or address is stored.
CREATE TABLE crash_reports (
  id text PRIMARY KEY CHECK (id ~ '^[A-HJ-NP-Z2-9]{8}$'),
  -- Chosen by the client per report, so a retried upload finds its report.
  submission_id uuid NOT NULL UNIQUE,
  kind text NOT NULL CHECK (kind IN ('problem')),
  install_id uuid NOT NULL,
  version text NOT NULL,
  version_minor integer NOT NULL,
  sim_revision integer NOT NULL,
  distribution text NOT NULL,
  platform text NOT NULL,
  os text NOT NULL,
  device_class text NOT NULL,
  device_tier text NOT NULL,
  description text NOT NULL,
  size integer NOT NULL CHECK (size >= 0),
  attachments jsonb NOT NULL DEFAULT '[]'::jsonb,
  bundle_key text,
  received_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX crash_reports_received_idx ON crash_reports (received_at DESC, id DESC);
CREATE UNIQUE INDEX crash_reports_bundle_key_idx ON crash_reports (bundle_key);
CREATE INDEX crash_reports_bundle_retention_idx ON crash_reports (received_at)
  WHERE bundle_key IS NOT NULL;
