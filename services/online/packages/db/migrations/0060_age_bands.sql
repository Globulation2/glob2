-- Age band of an account: social features (room and match chat, sharing
-- content, a chosen name or avatar) need '13_plus'. No birthdate is stored.
ALTER TABLE accounts
  ADD COLUMN age_band text NOT NULL DEFAULT 'unknown'
    CHECK (age_band IN ('unknown', 'under_13', '13_plus')),
  ADD COLUMN age_band_source text
    CHECK (age_band_source IN ('self', 'apple', 'google_play', 'moderator')),
  ADD COLUMN age_band_at timestamptz,
  ADD CONSTRAINT accounts_age_band_source_set
    CHECK ((age_band = 'unknown') = (age_band_source IS NULL));
