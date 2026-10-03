ALTER TABLE skin_purchases ADD COLUMN reconcile_after timestamptz NOT NULL DEFAULT now();
CREATE INDEX skin_purchases_reconcile_idx ON skin_purchases(reconcile_after)
  WHERE checkout_id IS NOT NULL;
