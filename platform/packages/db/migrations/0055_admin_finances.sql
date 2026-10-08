-- Reporting contains monetary facts only; no emails, card data or customer credit prices.
CREATE TABLE admin_payment_totals (
 product text NOT NULL, purchase_id text NOT NULL, provider_id text,
 mode text NOT NULL CHECK(mode IN ('live','test','unclassified')),
 currency text, paid_amount bigint, refunded_amount bigint NOT NULL DEFAULT 0,
 disputed boolean NOT NULL DEFAULT false, revision integer NOT NULL DEFAULT 0,
 historical boolean NOT NULL DEFAULT false,
 PRIMARY KEY(product,purchase_id), CHECK(paid_amount IS NULL OR paid_amount>=0),CHECK(refunded_amount>=0)
);
CREATE TABLE admin_financial_events (
 id text PRIMARY KEY, product text NOT NULL, purchase_id text NOT NULL,
 provider_id text, mode text NOT NULL CHECK(mode IN ('live','test','unclassified')),
 currency text NOT NULL, kind text NOT NULL CHECK(kind IN ('payment','refund','dispute','dispute_closed')),
 amount bigint NOT NULL CHECK(amount>=0), occurred_at timestamptz NOT NULL,
 recorded_at timestamptz NOT NULL DEFAULT now(), historical boolean NOT NULL DEFAULT false
);
CREATE INDEX admin_financial_events_period_idx ON admin_financial_events(mode,occurred_at,product);
CREATE TABLE admin_provider_attempts (
 product text NOT NULL, attempt_id text NOT NULL, request_id text NOT NULL,
 model text NOT NULL, stage text NOT NULL, status text NOT NULL, usage jsonb,
 created_at timestamptz NOT NULL, PRIMARY KEY(product,attempt_id)
);
CREATE INDEX admin_provider_attempts_period_idx ON admin_provider_attempts(created_at,product);
CREATE TABLE admin_provider_rates (
 version text NOT NULL, model text NOT NULL, currency text NOT NULL, effective_at timestamptz NOT NULL,
 input_micros bigint NOT NULL CHECK(input_micros>=0),cached_input_micros bigint NOT NULL CHECK(cached_input_micros>=0),
 output_micros bigint NOT NULL CHECK(output_micros>=0),call_micros bigint NOT NULL CHECK(call_micros>=0),
 PRIMARY KEY(version,model)
);
CREATE FUNCTION admin_provider_attempt() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE j jsonb=to_jsonb(NEW); u jsonb;
BEGIN
 IF NOT coalesce((SELECT collection FROM admin_analytics_settings WHERE id),false) THEN RETURN NEW; END IF;
 u=coalesce(j->'output'->'usage',j->'usage');
 INSERT INTO admin_provider_attempts(product,attempt_id,request_id,model,stage,status,usage,created_at)
 VALUES(TG_ARGV[0],j->>'id',coalesce(j->>'request_id',j->>'id'),coalesce(j->>'model',j->'rate'->>'model','unknown'),coalesce(j->>'stage','usage'),j->>'status',u,(j->>'created_at')::timestamptz)
 ON CONFLICT(product,attempt_id) DO UPDATE SET status=excluded.status,usage=coalesce(excluded.usage,admin_provider_attempts.usage);
 RETURN NEW;
END $$;
CREATE TRIGGER admin_provider AFTER INSERT OR UPDATE ON studio_attempts FOR EACH ROW EXECUTE FUNCTION admin_provider_attempt('maps');
INSERT INTO admin_provider_attempts SELECT 'maps',id,request_id,coalesce(model,'unknown'),stage,status,output->'usage',created_at FROM studio_attempts;
CREATE TRIGGER admin_provider AFTER INSERT OR UPDATE ON music_studio_attempts FOR EACH ROW EXECUTE FUNCTION admin_provider_attempt('music');
INSERT INTO admin_provider_attempts SELECT 'music',id,request_id,coalesce(model,'unknown'),stage,status,output->'usage',created_at FROM music_studio_attempts;
CREATE TRIGGER admin_provider AFTER INSERT OR UPDATE ON terrain_studio_attempts FOR EACH ROW EXECUTE FUNCTION admin_provider_attempt('terrain');
INSERT INTO admin_provider_attempts SELECT 'terrain',id,request_id,coalesce(model,'unknown'),stage,status,output->'usage',created_at FROM terrain_studio_attempts;
CREATE TRIGGER admin_provider AFTER INSERT OR UPDATE ON building_studio_attempts FOR EACH ROW EXECUTE FUNCTION admin_provider_attempt('buildings');
INSERT INTO admin_provider_attempts SELECT 'buildings',id,request_id,coalesce(model,'unknown'),stage,status,output->'usage',created_at FROM building_studio_attempts;
CREATE TRIGGER admin_provider AFTER INSERT OR UPDATE ON ai_studio_calls FOR EACH ROW EXECUTE FUNCTION admin_provider_attempt('aiStudio');
INSERT INTO admin_provider_attempts SELECT 'aiStudio',id,id,coalesce(rate->>'model','unknown'),'usage',status,usage,created_at FROM ai_studio_calls;
CREATE TRIGGER admin_provider AFTER INSERT OR UPDATE ON hive_calls FOR EACH ROW EXECUTE FUNCTION admin_provider_attempt('hive');
INSERT INTO admin_provider_attempts SELECT 'hive',id,id,coalesce(rate->>'model','unknown'),'usage',status,usage,created_at FROM hive_calls;
INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,currency,paid_amount,historical) SELECT 'maps',p.id,p.payment_id,'unclassified',p.pack->>'currency',(p.pack->>'amount')::bigint,true FROM map_purchases p WHERE p.paid;
INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at,historical) SELECT 'backfill:maps:'||p.id,'maps',p.id,p.payment_id,'unclassified',p.pack->>'currency','payment',(p.pack->>'amount')::bigint,l.created_at,true FROM map_purchases p JOIN map_ledger l ON l.id='purchase:'||p.id WHERE p.paid;
INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,currency,paid_amount,historical) SELECT 'music',p.id,p.payment_id,'unclassified',p.pack->>'currency',(p.pack->>'amount')::bigint,true FROM music_purchases p WHERE p.paid;
INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at,historical) SELECT 'backfill:music:'||p.id,'music',p.id,p.payment_id,'unclassified',p.pack->>'currency','payment',(p.pack->>'amount')::bigint,l.created_at,true FROM music_purchases p JOIN music_ledger l ON l.id='purchase:'||p.id WHERE p.paid;
INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,currency,paid_amount,historical) SELECT 'terrain',p.id,p.payment_id,'unclassified',p.pack->>'currency',(p.pack->>'amount')::bigint,true FROM terrain_purchases p WHERE p.paid;
INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at,historical) SELECT 'backfill:terrain:'||p.id,'terrain',p.id,p.payment_id,'unclassified',p.pack->>'currency','payment',(p.pack->>'amount')::bigint,l.created_at,true FROM terrain_purchases p JOIN terrain_ledger l ON l.id='purchase:'||p.id WHERE p.paid;
INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,currency,paid_amount,historical) SELECT 'buildings',p.id,p.payment_id,'unclassified',p.pack->>'currency',(p.pack->>'amount')::bigint,true FROM building_purchases p WHERE p.paid;
INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at,historical) SELECT 'backfill:buildings:'||p.id,'buildings',p.id,p.payment_id,'unclassified',p.pack->>'currency','payment',(p.pack->>'amount')::bigint,l.created_at,true FROM building_purchases p JOIN building_ledger l ON l.id='purchase:'||p.id WHERE p.paid;
INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,currency,paid_amount,historical) SELECT 'aiStudio',p.id,p.payment_id,'unclassified',p.pack->>'currency',(p.pack->>'amount')::bigint,true FROM ai_studio_purchases p WHERE p.paid;
INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at,historical) SELECT 'backfill:aiStudio:'||p.id,'aiStudio',p.id,p.payment_id,'unclassified',p.pack->>'currency','payment',(p.pack->>'amount')::bigint,l.created_at,true FROM ai_studio_purchases p JOIN ai_studio_ledger l ON l.id='purchase:'||p.id WHERE p.paid;
INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,currency,paid_amount,historical) SELECT 'hive',p.id,p.payment_id,'unclassified',p.pack->>'currency',(p.pack->>'amount')::bigint,true FROM hive_purchases p WHERE p.paid;
INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at,historical) SELECT 'backfill:hive:'||p.id,'hive',p.id,p.payment_id,'unclassified',p.pack->>'currency','payment',(p.pack->>'amount')::bigint,l.created_at,true FROM hive_purchases p JOIN hive_ledger l ON l.id='purchase:'||p.id WHERE p.paid;
INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,historical) SELECT 'skins',id::text,payment_intent_id,'unclassified',true FROM skin_purchases WHERE status IN ('paid','refunded','disputed');
