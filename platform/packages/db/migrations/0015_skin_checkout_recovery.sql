ALTER TABLE skin_purchases ADD COLUMN recovery_cursor text;
DROP INDEX skin_purchases_reconcile_idx;
CREATE INDEX skin_purchases_reconcile_idx ON skin_purchases(reconcile_after);
