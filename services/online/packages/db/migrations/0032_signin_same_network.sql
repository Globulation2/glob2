-- Browser sign-in without typing a code: the game's network address (hashed)
-- is kept on the attempt, and a browser opening the link from the same
-- network goes straight on. Other browsers confirm with one click, comparing
-- the code the game shows.
ALTER TABLE signin_attempts ADD COLUMN requesting_network_hash sha256_hex;
