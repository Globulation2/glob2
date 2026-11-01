-- Rate limits shared by every platform-api replica (sign-in, guest creation,
-- browser sign-in attempts, password failures, uploads, chat). One row per
-- (bucket, key) holds a sliding-window counter: the current fixed window's
-- count plus the previous window's, weighted by how much of it still
-- overlaps the sliding window. UNLOGGED: counters are cheap to lose in a
-- crash and not worth WAL traffic; the worker prunes idle rows.
CREATE UNLOGGED TABLE rate_limits (
  bucket text NOT NULL CHECK (char_length(bucket) <= 64),
  key text NOT NULL CHECK (char_length(key) <= 256),
  window_start timestamptz NOT NULL,
  count integer NOT NULL DEFAULT 0,
  previous_count integer NOT NULL DEFAULT 0,
  PRIMARY KEY (bucket, key)
);
CREATE INDEX rate_limits_window_idx ON rate_limits (window_start);

-- Takes `cost` events from (bucket, key) if the sliding-window estimate stays
-- within `max` per `window_seconds`; cost 0 only reads the estimate. One round
-- trip, serialised per key by the row lock. Returns whether the events were
-- taken, the estimate afterwards, and the seconds until the current window ends.
CREATE FUNCTION rate_limit_take(
  p_bucket text,
  p_key text,
  p_max integer,
  p_window_seconds double precision,
  p_cost integer DEFAULT 1
) RETURNS TABLE (allowed boolean, estimate double precision, window_left double precision)
LANGUAGE plpgsql AS $$
DECLARE
  t timestamptz := clock_timestamp();
  r rate_limits%ROWTYPE;
  elapsed double precision;
BEGIN
  INSERT INTO rate_limits (bucket, key, window_start)
  VALUES (p_bucket, p_key, t)
  ON CONFLICT (bucket, key) DO NOTHING;
  SELECT * INTO r FROM rate_limits WHERE bucket = p_bucket AND key = p_key FOR UPDATE;
  elapsed := extract(epoch FROM t - r.window_start);
  IF elapsed >= 2 * p_window_seconds OR elapsed < 0 THEN
    r.previous_count := 0;
    r.count := 0;
    r.window_start := t;
    elapsed := 0;
  ELSIF elapsed >= p_window_seconds THEN
    r.previous_count := r.count;
    r.count := 0;
    r.window_start := r.window_start + make_interval(secs => p_window_seconds);
    elapsed := elapsed - p_window_seconds;
  END IF;
  estimate := r.previous_count * (1 - elapsed / p_window_seconds) + r.count;
  allowed := p_cost = 0 OR estimate + p_cost <= p_max;
  IF allowed AND p_cost > 0 THEN
    r.count := r.count + p_cost;
    estimate := estimate + p_cost;
  END IF;
  UPDATE rate_limits
  SET window_start = r.window_start, count = r.count, previous_count = r.previous_count
  WHERE bucket = p_bucket AND key = p_key;
  window_left := p_window_seconds - elapsed;
  RETURN NEXT;
END $$;
