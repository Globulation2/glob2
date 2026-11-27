-- Crash symbolication and grouping (docs/multiplayer/crash-reports.md).
--
-- Release workflows upload each shipped binary's symbols (crash_symbols) and so
-- register their version (crash_releases); crash reports are accepted only from
-- registered versions. The crash worker symbolicates each crash report against
-- those symbols, stores the crashing thread's frames on the report and files
-- it under the crash group of its top in-game frames.

ALTER TABLE crash_reports DROP CONSTRAINT crash_reports_kind_check;
ALTER TABLE crash_reports ADD CONSTRAINT crash_reports_kind_check
  CHECK (kind IN ('problem', 'crash', 'metrickit', 'web'));

CREATE TABLE crash_releases (
  version text PRIMARY KEY CHECK (version ~ '^[0-9]+(\.[0-9]+){2,3}$'),
  first_upload_at timestamptz NOT NULL DEFAULT now()
);

-- One Breakpad .sym (gzip) or Emscripten symbol map (gzip) per shipped module.
-- Kept for every release still installable and at least two years; nothing
-- deletes them automatically.
CREATE TABLE crash_symbols (
  kind text NOT NULL CHECK (kind IN ('breakpad', 'wasm')),
  -- The debug file name a minidump names (glob2, glob2.pdb, libmain.so), or glob2.wasm.
  module text NOT NULL CHECK (length(module) BETWEEN 1 AND 128),
  -- Breakpad debug identifier (33 or more hex digits), or the wasm build ID.
  debug_id text NOT NULL CHECK (debug_id ~ '^[0-9A-Fa-f]{32,40}$'),
  version text NOT NULL REFERENCES crash_releases (version),
  os text NOT NULL,
  arch text NOT NULL,
  size bigint NOT NULL CHECK (size >= 0),
  blob_key text NOT NULL UNIQUE,
  uploaded_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (kind, module, debug_id)
);

CREATE TABLE crash_groups (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  -- The top in-game frames, innermost first, joined by ' | '.
  signature text NOT NULL UNIQUE,
  first_version text NOT NULL,
  last_version text NOT NULL,
  first_seen_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz NOT NULL DEFAULT now()
);

ALTER TABLE crash_reports
  ADD COLUMN status text NOT NULL DEFAULT 'none'
    CHECK (status IN ('none', 'pending', 'done', 'failed')),
  ADD COLUMN group_id uuid REFERENCES crash_groups (id) ON DELETE SET NULL,
  -- The crash reason (SIGSEGV, EXCEPTION_ACCESS_VIOLATION_READ, ...) or why processing failed.
  ADD COLUMN reason text,
  -- The crashing thread's symbolicated frames, kept with the row for two years.
  ADD COLUMN frames jsonb,
  ADD COLUMN match_tick bigint,
  ADD COLUMN match_seed bigint,
  ADD COLUMN processed_at timestamptz;

CREATE INDEX crash_reports_group_idx ON crash_reports (group_id, received_at DESC)
  WHERE group_id IS NOT NULL;
CREATE INDEX crash_reports_pending_idx ON crash_reports (received_at)
  WHERE status = 'pending';
