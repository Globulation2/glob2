-- Record future terminal times rather than reconstructing old completions.
CREATE FUNCTION admin_completion_time() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
 IF NEW.status IN ('settled','completed','failed','cancelled') AND (TG_OP='INSERT' OR NEW.status IS DISTINCT FROM OLD.status) AND NEW.completed_at IS NULL THEN NEW.completed_at=now(); END IF;
 RETURN NEW;
END $$;
ALTER TABLE hive_calls ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON hive_calls FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
ALTER TABLE ai_studio_calls ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON ai_studio_calls FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
ALTER TABLE map_calls ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON map_calls FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
ALTER TABLE music_calls ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON music_calls FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
ALTER TABLE terrain_calls ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON terrain_calls FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
ALTER TABLE building_calls ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON building_calls FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
ALTER TABLE ai_studio_requests ADD COLUMN completed_at timestamptz;
CREATE TRIGGER admin_completion BEFORE INSERT OR UPDATE ON ai_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_completion_time();
CREATE TABLE admin_analytics_settings (
 id boolean PRIMARY KEY DEFAULT true CHECK(id), collection boolean NOT NULL DEFAULT true,
 started_at timestamptz NOT NULL DEFAULT now()
);
INSERT INTO admin_analytics_settings(id) VALUES(true);
CREATE TABLE account_activity_days (
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 day date NOT NULL, kind text NOT NULL CHECK(kind IN ('guest','registered')),
 PRIMARY KEY(account_id,day)
);
CREATE INDEX account_activity_days_day_idx ON account_activity_days(day,kind,account_id);
CREATE TABLE admin_daily_metrics (
 day date NOT NULL, metric text NOT NULL, dimension text NOT NULL DEFAULT '',
 value double precision NOT NULL DEFAULT 0,
 PRIMARY KEY(day,metric,dimension)
);
CREATE TABLE admin_metric_coverage (
 metric text PRIMARY KEY, since date NOT NULL, historical_incomplete boolean NOT NULL DEFAULT true
);
-- Transactional daily counters survive source retention. No names, prompts, or account IDs.
CREATE FUNCTION admin_metric_add(m text,d text,t timestamptz,v double precision) RETURNS void LANGUAGE plpgsql AS $$
BEGIN
 IF NOT coalesce((SELECT collection FROM admin_analytics_settings WHERE id),false) OR t IS NULL OR v=0 THEN RETURN; END IF;
 INSERT INTO admin_daily_metrics(day,metric,dimension,value) VALUES((t AT TIME ZONE 'UTC')::date,m,d,v)
 ON CONFLICT(day,metric,dimension) DO UPDATE SET value=greatest(0,admin_daily_metrics.value+excluded.value);
END $$;
CREATE FUNCTION admin_created_metric() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE j jsonb=to_jsonb(NEW); d text=TG_ARGV[1]; t timestamptz;
BEGIN
 IF TG_ARGV[0]='accounts.created' THEN d=j->>'kind'; END IF;
 t=coalesce((j->>'created_at')::timestamptz,(j->>'day')::date::timestamptz);
 PERFORM admin_metric_add(TG_ARGV[0],d,t,1); RETURN NEW;
END $$;
CREATE FUNCTION admin_status_metric() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE j jsonb=to_jsonb(NEW); prev jsonb; t timestamptz=(j->>'created_at')::timestamptz; product text=TG_ARGV[0];
BEGIN
 IF TG_OP='UPDATE' THEN prev=to_jsonb(OLD); END IF;
 IF prev IS NOT NULL AND prev->>'status'=j->>'status' AND prev->>'completed_at' IS NOT DISTINCT FROM j->>'completed_at' THEN RETURN NEW; END IF;
 IF prev IS NOT NULL THEN
  PERFORM admin_metric_add('status.'||product,coalesce(prev->>'status','unknown'),t,-1);
  IF prev->>'completed_at' IS NOT NULL AND prev->>'status' IN ('ready','completed','settled','succeeded') THEN
   PERFORM admin_metric_add('duration.'||product,'seconds',t,-greatest(0,extract(epoch FROM ((prev->>'completed_at')::timestamptz-t))));
   PERFORM admin_metric_add('duration.'||product,'samples',t,-1);
  END IF;
 END IF;
 PERFORM admin_metric_add('status.'||product,coalesce(j->>'status','unknown'),t,1);
 IF j->>'completed_at' IS NOT NULL AND j->>'status' IN ('ready','completed','settled','succeeded') THEN
  PERFORM admin_metric_add('duration.'||product,'seconds',t,greatest(0,extract(epoch FROM ((j->>'completed_at')::timestamptz-t))));
  PERFORM admin_metric_add('duration.'||product,'samples',t,1);
 END IF;
 RETURN NEW;
END $$;
CREATE FUNCTION admin_match_metric() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
 IF NEW.started_at IS NOT NULL AND (TG_OP='INSERT' OR OLD.started_at IS NULL) THEN PERFORM admin_metric_add('matches.started','',NEW.started_at,1); END IF;
 IF NEW.status='ended' AND (TG_OP='INSERT' OR OLD.status<>'ended') THEN PERFORM admin_metric_add('matches.completed','',coalesce(NEW.ended_at,now()),1); END IF;
 IF NEW.status='cancelled' AND (TG_OP='INSERT' OR OLD.status<>'cancelled') THEN PERFORM admin_metric_add('matches.cancelled','',now(),1); END IF;
 RETURN NEW;
END $$;
CREATE TRIGGER admin_matches AFTER INSERT OR UPDATE ON matches FOR EACH ROW EXECUTE FUNCTION admin_match_metric();
-- Existing durable match history can be counted; cancellation timestamps are not recorded.
INSERT INTO admin_daily_metrics SELECT (started_at AT TIME ZONE 'UTC')::date,'matches.started','',count(*) FROM matches WHERE started_at IS NOT NULL GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (ended_at AT TIME ZONE 'UTC')::date,'matches.completed','',count(*) FROM matches WHERE status='ended' AND ended_at IS NOT NULL GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES ('activity',current_date,true),('matches.started',coalesce((SELECT min(started_at)::date FROM matches),current_date),false),('matches.completed',coalesce((SELECT min(ended_at)::date FROM matches),current_date),false),('matches.cancelled',current_date,true);
CREATE TRIGGER admin_created AFTER INSERT ON accounts FOR EACH ROW EXECUTE FUNCTION admin_created_metric('accounts.created','');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'accounts.created',kind,count(*) FROM accounts GROUP BY 1,3 ON CONFLICT(day,metric,dimension) DO UPDATE SET value=greatest(0,admin_daily_metrics.value+excluded.value);
INSERT INTO admin_metric_coverage VALUES ('accounts.created:',coalesce((SELECT min(created_at)::date FROM accounts),current_date),true);
CREATE TABLE admin_library_publications(library text NOT NULL,version_id uuid NOT NULL,day date NOT NULL,PRIMARY KEY(library,version_id));
CREATE FUNCTION admin_record_publication(l text,v uuid,t timestamptz) RETURNS void LANGUAGE plpgsql AS $$
DECLARE n integer;
BEGIN
 IF NOT coalesce((SELECT collection FROM admin_analytics_settings WHERE id),false) THEN RETURN; END IF;
 INSERT INTO admin_library_publications VALUES(l,v,(t AT TIME ZONE 'UTC')::date) ON CONFLICT DO NOTHING;
 GET DIAGNOSTICS n=ROW_COUNT;
 IF n=1 THEN PERFORM admin_metric_add('library.published',l,t,1); END IF;
END $$;
CREATE FUNCTION admin_version_publication() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE j jsonb=to_jsonb(NEW); visible boolean;
BEGIN
 IF TG_ARGV[0]='maps' AND j->>'validation'<>'valid' THEN RETURN NEW; END IF;
 IF TG_ARGV[0]='skins' THEN visible=true; ELSE
 EXECUTE format('SELECT visibility=''public'' FROM %I WHERE id=$1',TG_ARGV[1]) INTO visible USING (j->>TG_ARGV[2])::uuid;
 END IF;
 IF visible THEN PERFORM admin_record_publication(TG_ARGV[0],(j->>'id')::uuid,now()); END IF;
 RETURN NEW;
END $$;
CREATE FUNCTION admin_parent_publication() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE v record;
BEGIN
 IF NEW.visibility<>'public' THEN RETURN NEW; END IF;
 FOR v IN EXECUTE format('SELECT id FROM %I WHERE %I=$1 %s',TG_ARGV[1],TG_ARGV[2],CASE WHEN TG_ARGV[0]='maps' THEN 'AND validation=''valid''' ELSE '' END) USING NEW.id LOOP
  PERFORM admin_record_publication(TG_ARGV[0],v.id,now());
 END LOOP;
 RETURN NEW;
END $$;
CREATE TRIGGER admin_created AFTER INSERT ON map_downloads FOR EACH ROW EXECUTE FUNCTION admin_created_metric('library.downloads','maps');
INSERT INTO admin_daily_metrics SELECT (day::timestamp AT TIME ZONE 'UTC' AT TIME ZONE 'UTC')::date,'library.downloads','maps',count(*) FROM map_downloads GROUP BY 1,3 ON CONFLICT(day,metric,dimension) DO UPDATE SET value=greatest(0,admin_daily_metrics.value+excluded.value);
INSERT INTO admin_metric_coverage VALUES ('library.downloads:maps',coalesce((SELECT min(day::timestamp AT TIME ZONE 'UTC')::date FROM map_downloads),current_date),true);
CREATE TRIGGER admin_created AFTER INSERT ON ai_downloads FOR EACH ROW EXECUTE FUNCTION admin_created_metric('library.downloads','ais');
INSERT INTO admin_daily_metrics SELECT (day::timestamp AT TIME ZONE 'UTC' AT TIME ZONE 'UTC')::date,'library.downloads','ais',count(*) FROM ai_downloads GROUP BY 1,3 ON CONFLICT(day,metric,dimension) DO UPDATE SET value=greatest(0,admin_daily_metrics.value+excluded.value);
INSERT INTO admin_metric_coverage VALUES ('library.downloads:ais',coalesce((SELECT min(day::timestamp AT TIME ZONE 'UTC')::date FROM ai_downloads),current_date),true);
CREATE TRIGGER admin_created AFTER INSERT ON set_downloads FOR EACH ROW EXECUTE FUNCTION admin_created_metric('library.downloads','sets');
INSERT INTO admin_daily_metrics SELECT (day::timestamp AT TIME ZONE 'UTC' AT TIME ZONE 'UTC')::date,'library.downloads','sets',count(*) FROM set_downloads GROUP BY 1,3 ON CONFLICT(day,metric,dimension) DO UPDATE SET value=greatest(0,admin_daily_metrics.value+excluded.value);
INSERT INTO admin_metric_coverage VALUES ('library.downloads:sets',coalesce((SELECT min(day::timestamp AT TIME ZONE 'UTC')::date FROM set_downloads),current_date),true);
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON studio_requests FOR EACH ROW EXECUTE FUNCTION admin_status_metric('maps');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'status.maps',status,count(*) FROM studio_requests GROUP BY 1,3;
INSERT INTO admin_metric_coverage VALUES ('status.maps',coalesce((SELECT min(created_at)::date FROM studio_requests),current_date),true);
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON music_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_status_metric('music');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'status.music',status,count(*) FROM music_studio_requests GROUP BY 1,3;
INSERT INTO admin_metric_coverage VALUES ('status.music',coalesce((SELECT min(created_at)::date FROM music_studio_requests),current_date),true);
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON terrain_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_status_metric('terrain');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'status.terrain',status,count(*) FROM terrain_studio_requests GROUP BY 1,3;
INSERT INTO admin_metric_coverage VALUES ('status.terrain',coalesce((SELECT min(created_at)::date FROM terrain_studio_requests),current_date),true);
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON building_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_status_metric('buildings');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'status.buildings',status,count(*) FROM building_studio_requests GROUP BY 1,3;
INSERT INTO admin_metric_coverage VALUES ('status.buildings',coalesce((SELECT min(created_at)::date FROM building_studio_requests),current_date),true);
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON ai_studio_requests FOR EACH ROW EXECUTE FUNCTION admin_status_metric('aiStudio');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'status.aiStudio',status,count(*) FROM ai_studio_requests GROUP BY 1,3;
INSERT INTO admin_metric_coverage VALUES ('status.aiStudio',coalesce((SELECT min(created_at)::date FROM ai_studio_requests),current_date),true);
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON hive_calls FOR EACH ROW EXECUTE FUNCTION admin_status_metric('hive');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'status.hive',status,count(*) FROM hive_calls GROUP BY 1,3;
INSERT INTO admin_metric_coverage VALUES ('status.hive',coalesce((SELECT min(created_at)::date FROM hive_calls),current_date),true);
CREATE TRIGGER admin_status AFTER INSERT OR UPDATE ON engine_jobs FOR EACH ROW EXECUTE FUNCTION admin_status_metric('engine');
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'status.engine',status,count(*) FROM engine_jobs GROUP BY 1,3;
INSERT INTO admin_metric_coverage VALUES ('status.engine',coalesce((SELECT min(created_at)::date FROM engine_jobs),current_date),true);
CREATE FUNCTION admin_download_counter() RETURNS trigger LANGUAGE plpgsql AS $$
 BEGIN
 PERFORM admin_metric_add('library.downloads',TG_ARGV[0],now(),greatest(0,(to_jsonb(NEW)->>TG_ARGV[1])::int-(to_jsonb(OLD)->>TG_ARGV[1])::int)); RETURN NEW;
 END $$;
 CREATE TRIGGER admin_downloads AFTER UPDATE OF download_count ON building_families FOR EACH ROW EXECUTE FUNCTION admin_download_counter('buildings','download_count');
 CREATE TRIGGER admin_downloads AFTER UPDATE OF downloads ON music_releases FOR EACH ROW EXECUTE FUNCTION admin_download_counter('music','downloads');
 INSERT INTO admin_metric_coverage VALUES('library.downloads:buildings',current_date,true),('library.downloads:music',current_date,true);
 CREATE FUNCTION admin_active_metric() RETURNS trigger LANGUAGE plpgsql AS $$
 BEGIN
 IF TG_OP='UPDATE' THEN
 IF OLD.kind=NEW.kind THEN RETURN NEW; END IF;
 PERFORM admin_metric_add('activity',OLD.kind,OLD.day::timestamp AT TIME ZONE 'UTC',-1);
 END IF;
 PERFORM admin_metric_add('activity',NEW.kind,NEW.day::timestamp AT TIME ZONE 'UTC',1); RETURN NEW;
 END $$;
 CREATE TRIGGER admin_active AFTER INSERT OR UPDATE ON account_activity_days FOR EACH ROW EXECUTE FUNCTION admin_active_metric();
CREATE FUNCTION admin_music_publication() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
 IF NEW.status='published' AND (TG_OP='INSERT' OR OLD.status<>'published') THEN
 PERFORM admin_record_publication('music',NEW.id,now());
 END IF;
 RETURN NEW;
END $$;
CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE ON music_releases FOR EACH ROW EXECUTE FUNCTION admin_music_publication();
INSERT INTO admin_metric_coverage VALUES('library.published:music',current_date,true);

CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE ON map_versions FOR EACH ROW EXECUTE FUNCTION admin_version_publication('maps','maps','map_id');
CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE OF visibility ON maps FOR EACH ROW EXECUTE FUNCTION admin_parent_publication('maps','map_versions','map_id');
INSERT INTO admin_library_publications SELECT 'maps',v.id,(v.created_at AT TIME ZONE 'UTC')::date FROM map_versions v JOIN maps p ON p.id=v.map_id WHERE p.visibility='public' AND v.validation='valid';
INSERT INTO admin_daily_metrics SELECT day,'library.published',library,count(*) FROM admin_library_publications WHERE library='maps' GROUP BY day,library;
INSERT INTO admin_metric_coverage VALUES('library.published:maps',coalesce((SELECT min(day) FROM admin_library_publications WHERE library='maps'),(now() AT TIME ZONE 'UTC')::date),true);

CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE ON ai_versions FOR EACH ROW EXECUTE FUNCTION admin_version_publication('ais','ais','ai_id');
CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE OF visibility ON ais FOR EACH ROW EXECUTE FUNCTION admin_parent_publication('ais','ai_versions','ai_id');
INSERT INTO admin_library_publications SELECT 'ais',v.id,(v.created_at AT TIME ZONE 'UTC')::date FROM ai_versions v JOIN ais p ON p.id=v.ai_id WHERE p.visibility='public';
INSERT INTO admin_daily_metrics SELECT day,'library.published',library,count(*) FROM admin_library_publications WHERE library='ais' GROUP BY day,library;
INSERT INTO admin_metric_coverage VALUES('library.published:ais',coalesce((SELECT min(day) FROM admin_library_publications WHERE library='ais'),(now() AT TIME ZONE 'UTC')::date),true);

CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE ON building_releases FOR EACH ROW EXECUTE FUNCTION admin_version_publication('buildings','building_families','family_id');
CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE OF visibility ON building_families FOR EACH ROW EXECUTE FUNCTION admin_parent_publication('buildings','building_releases','family_id');
INSERT INTO admin_library_publications SELECT 'buildings',v.id,(v.created_at AT TIME ZONE 'UTC')::date FROM building_releases v JOIN building_families p ON p.id=v.family_id WHERE p.visibility='public';
INSERT INTO admin_daily_metrics SELECT day,'library.published',library,count(*) FROM admin_library_publications WHERE library='buildings' GROUP BY day,library;
INSERT INTO admin_metric_coverage VALUES('library.published:buildings',coalesce((SELECT min(day) FROM admin_library_publications WHERE library='buildings'),(now() AT TIME ZONE 'UTC')::date),true);

CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE ON set_versions FOR EACH ROW EXECUTE FUNCTION admin_version_publication('sets','asset_sets','set_id');
CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE OF visibility ON asset_sets FOR EACH ROW EXECUTE FUNCTION admin_parent_publication('sets','set_versions','set_id');
INSERT INTO admin_library_publications SELECT 'sets',v.id,(v.created_at AT TIME ZONE 'UTC')::date FROM set_versions v JOIN asset_sets p ON p.id=v.set_id WHERE p.visibility='public';
INSERT INTO admin_daily_metrics SELECT day,'library.published',library,count(*) FROM admin_library_publications WHERE library='sets' GROUP BY day,library;
INSERT INTO admin_metric_coverage VALUES('library.published:sets',coalesce((SELECT min(day) FROM admin_library_publications WHERE library='sets'),(now() AT TIME ZONE 'UTC')::date),true);

CREATE TRIGGER admin_publication AFTER INSERT OR UPDATE ON colony_skin_versions FOR EACH ROW EXECUTE FUNCTION admin_version_publication('skins','colony_skins','skin_id');
INSERT INTO admin_library_publications SELECT 'skins',v.id,(v.created_at AT TIME ZONE 'UTC')::date FROM colony_skin_versions v JOIN colony_skins p ON p.id=v.skin_id;
INSERT INTO admin_daily_metrics SELECT day,'library.published',library,count(*) FROM admin_library_publications WHERE library='skins' GROUP BY day,library;
INSERT INTO admin_metric_coverage VALUES('library.published:skins',coalesce((SELECT min(day) FROM admin_library_publications WHERE library='skins'),(now() AT TIME ZONE 'UTC')::date),true);

INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.maps','seconds',sum(greatest(0,extract(epoch FROM completed_at-created_at))) FROM studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.maps','samples',count(*) FROM studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES('duration.maps',coalesce((SELECT min(created_at)::date FROM studio_requests WHERE completed_at IS NOT NULL),(now() AT TIME ZONE 'UTC')::date),true);

INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.music','seconds',sum(greatest(0,extract(epoch FROM completed_at-created_at))) FROM music_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.music','samples',count(*) FROM music_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES('duration.music',coalesce((SELECT min(created_at)::date FROM music_studio_requests WHERE completed_at IS NOT NULL),(now() AT TIME ZONE 'UTC')::date),true);

INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.terrain','seconds',sum(greatest(0,extract(epoch FROM completed_at-created_at))) FROM terrain_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.terrain','samples',count(*) FROM terrain_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES('duration.terrain',coalesce((SELECT min(created_at)::date FROM terrain_studio_requests WHERE completed_at IS NOT NULL),(now() AT TIME ZONE 'UTC')::date),true);

INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.buildings','seconds',sum(greatest(0,extract(epoch FROM completed_at-created_at))) FROM building_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.buildings','samples',count(*) FROM building_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES('duration.buildings',coalesce((SELECT min(created_at)::date FROM building_studio_requests WHERE completed_at IS NOT NULL),(now() AT TIME ZONE 'UTC')::date),true);

INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.aiStudio','seconds',sum(greatest(0,extract(epoch FROM completed_at-created_at))) FROM ai_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.aiStudio','samples',count(*) FROM ai_studio_requests WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES('duration.aiStudio',coalesce((SELECT min(created_at)::date FROM ai_studio_requests WHERE completed_at IS NOT NULL),(now() AT TIME ZONE 'UTC')::date),true);

INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.hive','seconds',sum(greatest(0,extract(epoch FROM completed_at-created_at))) FROM hive_calls WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.hive','samples',count(*) FROM hive_calls WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES('duration.hive',coalesce((SELECT min(created_at)::date FROM hive_calls WHERE completed_at IS NOT NULL),(now() AT TIME ZONE 'UTC')::date),true);

INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.engine','seconds',sum(greatest(0,extract(epoch FROM completed_at-created_at))) FROM engine_jobs WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_daily_metrics SELECT (created_at AT TIME ZONE 'UTC')::date,'duration.engine','samples',count(*) FROM engine_jobs WHERE completed_at IS NOT NULL AND status IN ('ready','completed','settled','succeeded') GROUP BY 1;
INSERT INTO admin_metric_coverage VALUES('duration.engine',coalesce((SELECT min(created_at)::date FROM engine_jobs WHERE completed_at IS NOT NULL),(now() AT TIME ZONE 'UTC')::date),true);

CREATE FUNCTION admin_registration_conversion() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
 IF OLD.kind='guest' AND NEW.kind='registered' AND coalesce((SELECT collection FROM admin_analytics_settings WHERE id),false) THEN
  PERFORM admin_metric_add('accounts.created','registered',now(),1);
  UPDATE account_activity_days SET kind='registered' WHERE account_id=NEW.id AND day=(now() AT TIME ZONE 'UTC')::date;
 END IF;
 RETURN NEW;
END $$;
CREATE TRIGGER admin_registration AFTER UPDATE OF kind ON accounts FOR EACH ROW EXECUTE FUNCTION admin_registration_conversion();
