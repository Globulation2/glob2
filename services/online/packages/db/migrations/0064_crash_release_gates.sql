-- Release gates for crash reports (docs/multiplayer/crash-reports.md):
-- crash-free session counts, crash group triage and GitHub issues.

-- Sessions that installs whose player chose "Always send" started, per UTC day,
-- release, platform and channel. One row per install and day holds that
-- install's running total (the client resends it, the largest is kept) and is
-- what distinct installs are counted from; both tables are deleted after two
-- years, like crash report rows. No address is stored.
CREATE TABLE crash_session_installs (
  day date NOT NULL,
  version text NOT NULL REFERENCES crash_releases (version),
  platform text NOT NULL,
  distribution text NOT NULL,
  install_id uuid NOT NULL,
  sessions integer NOT NULL CHECK (sessions >= 0),
  PRIMARY KEY (install_id, day, version, platform, distribution)
);
CREATE INDEX crash_session_installs_version_idx ON crash_session_installs (version, install_id);
CREATE INDEX crash_session_installs_day_idx ON crash_session_installs (day);

CREATE TABLE crash_session_days (
  day date NOT NULL,
  version text NOT NULL REFERENCES crash_releases (version),
  platform text NOT NULL,
  distribution text NOT NULL,
  sessions bigint NOT NULL DEFAULT 0 CHECK (sessions >= 0),
  -- Distinct installs that reported sessions that day.
  installs integer NOT NULL DEFAULT 0 CHECK (installs >= 0),
  PRIMARY KEY (day, version, platform, distribution)
);
CREATE INDEX crash_session_days_version_idx ON crash_session_days (version);

-- Triage: every group starts 'new'; 'resolved' names the first release with
-- the fix, and the crash worker sets a resolved group back to 'new' when it
-- recurs in that release or a later one (fixed_in stays, as history).
ALTER TABLE crash_groups
  ADD COLUMN state text NOT NULL DEFAULT 'new' CHECK (state IN ('new', 'triaged', 'resolved')),
  ADD COLUMN fixed_in text CHECK (fixed_in ~ '^[0-9]+(\.[0-9]+){2,3}$'),
  ADD COLUMN state_changed_at timestamptz,
  -- The issue opened on CRASH_GITHUB_REPOSITORY for the group.
  ADD COLUMN github_issue integer CHECK (github_issue > 0),
  ADD COLUMN github_issue_url text,
  ADD CONSTRAINT crash_groups_resolved_fixed_in CHECK (state <> 'resolved' OR fixed_in IS NOT NULL);
CREATE INDEX crash_groups_first_version_idx ON crash_groups (first_version);

CREATE INDEX crash_reports_version_idx ON crash_reports (version, install_id)
  WHERE kind <> 'problem';
