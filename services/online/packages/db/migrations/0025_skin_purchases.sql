CREATE TABLE skin_purchases (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id uuid NOT NULL REFERENCES accounts(id),
  request_id uuid NOT NULL,
  sku text NOT NULL CHECK (sku IN ('designer', 'stripes', 'spots')),
  entitlement text NOT NULL,
  price_id text NOT NULL,
  checkout_id text UNIQUE,
  payment_intent_id text UNIQUE,
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','paid','refunded','disputed','failed')),
  entitlement_id uuid REFERENCES entitlements(id),
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(account_id, request_id)
);
CREATE INDEX skin_purchases_account_idx ON skin_purchases(account_id, created_at);
CREATE TABLE skin_payment_events (
  id text PRIMARY KEY,
  event_type text NOT NULL,
  purchase_id uuid REFERENCES skin_purchases(id),
  processed_at timestamptz NOT NULL DEFAULT now()
);
