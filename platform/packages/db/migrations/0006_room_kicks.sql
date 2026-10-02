-- Room kicks: a player the host removed cannot rejoin that room until
-- `until` (room.kick). Expired rows are deleted by the room sweep.
CREATE TABLE room_kicks (
  room_id uuid NOT NULL REFERENCES rooms (id) ON DELETE CASCADE,
  account_id uuid NOT NULL REFERENCES accounts (id) ON DELETE CASCADE,
  kicked_by_account_id uuid REFERENCES accounts (id) ON DELETE SET NULL,
  until timestamptz NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (room_id, account_id)
);
CREATE INDEX room_kicks_until_idx ON room_kicks (until);
