-- Wire encoding changes without overwriting published skin/map source content.
CREATE TABLE image_webp_renditions (
 source_sha256 sha256_hex PRIMARY KEY REFERENCES blobs(sha256) ON DELETE CASCADE,
 webp_sha256 sha256_hex NOT NULL REFERENCES blobs(sha256)
);
CREATE INDEX image_webp_renditions_target ON image_webp_renditions(webp_sha256);
