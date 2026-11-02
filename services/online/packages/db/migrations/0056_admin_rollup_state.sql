-- Keep the contribution actually counted, rather than assuming every source
-- transition was observed. Collection can pause while source state changes.
-- These opaque request IDs have no account linkage; remove them with the source,
-- while retaining the anonymous daily totals through normal source cleanup.
CREATE TABLE admin_metric_sources (
  product text NOT NULL,
  source_id text NOT NULL,
  created_at timestamptz NOT NULL,
  status text NOT NULL,
  duration_seconds double precision,
  PRIMARY KEY (product, source_id)
);

CREATE OR REPLACE FUNCTION admin_status_metric() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE
  j jsonb = to_jsonb(NEW);
  p text = TG_ARGV[0];
  source text = j->>'id';
  created timestamptz = (j->>'created_at')::timestamptz;
  state text = coalesce(j->>'status', 'unknown');
  duration double precision;
  previous admin_metric_sources%ROWTYPE;
BEGIN
  IF NOT coalesce((SELECT collection FROM admin_analytics_settings WHERE id), false) THEN
    RETURN NEW;
  END IF;
  IF j->>'completed_at' IS NOT NULL AND state IN ('ready','completed','settled','succeeded') THEN
    duration = greatest(0, extract(epoch FROM ((j->>'completed_at')::timestamptz - created)));
  END IF;
  -- Source-row updates serialize this replacement within their transaction.
  SELECT * INTO previous FROM admin_metric_sources WHERE product = p AND source_id = source;
  IF FOUND THEN
    IF previous.status = state AND previous.created_at = created
       AND previous.duration_seconds IS NOT DISTINCT FROM duration THEN
      RETURN NEW;
    END IF;
    PERFORM admin_metric_add('status.' || p, previous.status, previous.created_at, -1);
    IF previous.duration_seconds IS NOT NULL THEN
      PERFORM admin_metric_add('duration.' || p, 'seconds', previous.created_at, -previous.duration_seconds);
      PERFORM admin_metric_add('duration.' || p, 'samples', previous.created_at, -1);
    END IF;
  END IF;
  PERFORM admin_metric_add('status.' || p, state, created, 1);
  IF duration IS NOT NULL THEN
    PERFORM admin_metric_add('duration.' || p, 'seconds', created, duration);
    PERFORM admin_metric_add('duration.' || p, 'samples', created, 1);
  END IF;
  INSERT INTO admin_metric_sources VALUES (p, source, created, state, duration)
    ON CONFLICT (product, source_id) DO UPDATE SET
      created_at = excluded.created_at, status = excluded.status,
      duration_seconds = excluded.duration_seconds;
  RETURN NEW;
END $$;

CREATE FUNCTION admin_forget_metric_source() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  DELETE FROM admin_metric_sources WHERE product = TG_ARGV[0] AND source_id = OLD.id::text;
  RETURN OLD;
END $$;

-- Migration 0054 already backfilled these retained sources. Establish their
-- replacement baseline without replaying or incrementing those totals.
DO $$
DECLARE source record;
BEGIN
  FOR source IN SELECT * FROM (VALUES
    ('studio_requests', 'maps'), ('music_studio_requests', 'music'),
    ('terrain_studio_requests', 'terrain'), ('building_studio_requests', 'buildings'),
    ('ai_studio_requests', 'aiStudio'), ('hive_calls', 'hive'), ('engine_jobs', 'engine')
  ) AS sources(table_name, product)
  LOOP
    EXECUTE format(
      'INSERT INTO admin_metric_sources SELECT $1, id::text, created_at, status,
       CASE WHEN completed_at IS NOT NULL AND status IN (''ready'',''completed'',''settled'',''succeeded'')
         THEN greatest(0, extract(epoch FROM completed_at - created_at)) END FROM %I', source.table_name
    ) USING source.product;
    EXECUTE format(
      'CREATE TRIGGER admin_forget_metric AFTER DELETE ON %I FOR EACH ROW EXECUTE FUNCTION admin_forget_metric_source(%L)',
      source.table_name, source.product
    );
  END LOOP;
END $$;

-- Period reporting must not scan lifetime credit ledgers. Account-leading
-- indexes serve wallet history, while these serve bounded admin date ranges.
DO $$
DECLARE prefix text;
BEGIN
  FOREACH prefix IN ARRAY ARRAY['map','music','terrain','building','ai_studio','hive'] LOOP
    EXECUTE format('CREATE INDEX %I ON %I (created_at, kind)',
      prefix || '_ledger_admin_period_idx', prefix || '_ledger');
  END LOOP;
  FOREACH prefix IN ARRAY ARRAY['ai_studio','hive'] LOOP
    EXECUTE format('CREATE INDEX %I ON %I (completed_at) WHERE status = ''settled''',
      prefix || '_calls_admin_returned_idx', prefix || '_calls');
    EXECUTE format('CREATE INDEX %I ON %I (created_at, id) WHERE status = ''uncertain''',
      prefix || '_calls_admin_uncertain_idx', prefix || '_calls');
  END LOOP;
  FOREACH prefix IN ARRAY ARRAY['studio_requests','music_studio_requests','terrain_studio_requests','building_studio_requests'] LOOP
    EXECUTE format('CREATE INDEX %I ON %I (created_at, id) WHERE status = ''uncertain''',
      prefix || '_admin_uncertain_idx', prefix);
  END LOOP;
END $$;
CREATE INDEX admin_provider_attempts_request_idx ON admin_provider_attempts (product, request_id, stage);
