-- Uncaught Java exceptions from the Android game (docs/multiplayer/crash-reports.md).
ALTER TABLE crash_reports DROP CONSTRAINT crash_reports_kind_check;
ALTER TABLE crash_reports ADD CONSTRAINT crash_reports_kind_check
  CHECK (kind IN ('problem', 'crash', 'metrickit', 'web', 'java'));
