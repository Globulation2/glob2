-- Non-fatal error reports from players who chose Always send
-- (docs/multiplayer/crash-reports.md#non-fatal-errors).
ALTER TABLE crash_reports DROP CONSTRAINT crash_reports_kind_check;
ALTER TABLE crash_reports ADD CONSTRAINT crash_reports_kind_check
  CHECK (kind IN ('problem', 'crash', 'metrickit', 'web', 'java', 'error'));
