-- Browser sign-in: the player types the code their game shows into the
-- browser before any sign-in method is offered (RFC 8628 §5.4: someone who
-- sends a victim a sign-in link cannot also show them the code). The attempt
-- is bound to the browser that enters the right code; wrong codes count, and
-- the attempt fails after a few.
ALTER TABLE signin_attempts
  ADD COLUMN code_confirmed_at timestamptz,
  ADD COLUMN code_failures smallint NOT NULL DEFAULT 0 CHECK (code_failures >= 0);
