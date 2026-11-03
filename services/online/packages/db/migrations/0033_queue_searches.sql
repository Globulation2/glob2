-- One quick-match search may enter several queues at once (Ranked and Casual 1v1
-- together): it holds an active ticket in each, sharing search_id. A player has at
-- most one active ticket per queue; one active search per account is enforced by
-- joinQueues, which locks the account row. Existing tickets are searches of one.
ALTER TABLE queue_tickets ADD COLUMN search_id uuid;
UPDATE queue_tickets SET search_id = id;
ALTER TABLE queue_tickets ALTER COLUMN search_id SET NOT NULL;
-- A ticket inserted on its own is a search of one.
ALTER TABLE queue_tickets ALTER COLUMN search_id SET DEFAULT gen_random_uuid();

DROP INDEX queue_tickets_one_active_idx;
CREATE UNIQUE INDEX queue_tickets_one_active_idx ON queue_tickets (account_id, queue_id)
  WHERE status IN ('waiting', 'proposed');
CREATE INDEX queue_tickets_search_idx ON queue_tickets (search_id);
