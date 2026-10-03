-- Network telemetry from the relay (docs/development/network-telemetry.md). The
-- relay's end-of-match report (RelayMatchEnded.network, RelayNetworkSummary v1)
-- carries one entry per human seat; the intake stores each seat's entry with
-- its participant so the match page can show connection quality. NULL: the
-- relay sent no summary (older relays), the seat is an AI, or the match ended
-- before this column existed. The whole report, match-level counters
-- included, stays in matches.end_report as before.
ALTER TABLE match_participants ADD COLUMN network jsonb;
