-- Resolution metadata for libraries whose original report format only had a boolean.
CREATE TABLE admin_report_resolutions (
 library text NOT NULL CHECK(library IN ('maps','ais','buildings','sets','skins','music')),
 report_id uuid NOT NULL,
 resolution text NOT NULL CHECK(resolution IN ('resolved','dismissed')),
 reason text NOT NULL,
 actor_id uuid REFERENCES accounts(id) ON DELETE SET NULL,
 resolved_at timestamptz NOT NULL DEFAULT now(),
 PRIMARY KEY(library,report_id)
);
CREATE INDEX admin_audit_page_idx ON admin_audit_log(created_at DESC,id DESC);
CREATE INDEX map_reports_admin_page_idx ON map_reports(created_at DESC,id DESC);
CREATE INDEX ai_reports_admin_page_idx ON ai_reports(created_at DESC,id DESC);
CREATE INDEX building_reports_admin_page_idx ON building_reports(created_at DESC,id DESC);
CREATE INDEX set_reports_admin_page_idx ON set_reports(created_at DESC,id DESC);
CREATE INDEX music_reports_admin_page_idx ON music_reports(created_at DESC,id DESC);
CREATE INDEX colony_skin_reports_admin_page_idx ON colony_skin_reports(created_at DESC,id DESC);

CREATE INDEX admin_audit_actor_idx ON admin_audit_log(actor_account_id,created_at DESC,id DESC);
CREATE INDEX admin_audit_action_idx ON admin_audit_log(action,created_at DESC,id DESC);
CREATE INDEX admin_audit_target_idx ON admin_audit_log(target_type,target_id,created_at DESC,id DESC);
