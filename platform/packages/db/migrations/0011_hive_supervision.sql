-- A new bounded command must not revoke existing standing-order supervision.
ALTER TABLE hive_operations ADD COLUMN supervised boolean NOT NULL DEFAULT false;
ALTER TABLE hive_programs ADD COLUMN supervised boolean NOT NULL DEFAULT false;
UPDATE hive_programs p SET supervised=s.supervision FROM hive_sessions s WHERE s.id=p.session_id;
