# Configurable units review evidence

This branch retains development evidence for draft PR #1057. Each archive keeps
its original source and binary producer identities. It is not a claim that all
final acceptance gates have passed. The PR verification comment reports those
gates separately.

The initial packages contain the complete-state comparison corpus through release
15 and the paired release-16 tournament. A later supplement supplies the fresh
release-17 execution audit and its explicit unchanged-byte proof transfer.
No producer is renamed as a later source revision.

Concatenate each archive's `.partNN` files in lexicographic order, verify the
archive SHA-256 against its `*-parts.json`, then extract it. Individual part hashes
and sizes are also in that index. Archives restore repository-relative paths
under `artifacts/unit-foundation/`. Source/binary provenance, commands and
comparison scripts are included. Additional restore instructions accompany the
final supplement. The large-map package is an uncompressed tar; the other two
are gzip-compressed tar archives.

The state archive intentionally selects complete raw captures rather than
duplicating every many-gigabyte diagnostic sidecar. Its file inventories and
reproduction commands identify this limit explicitly. The paired tournament
contains every result, input, replay, final save and checksum trace; separate
inventories identify omitted diagnostic sidecar bytes.
