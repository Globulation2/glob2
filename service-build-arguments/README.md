# Service build argument validation

Tested source: 40a8f1c938dfb2fb1218ed5fb38987802bfa1f5d; base: aaa73b56543e7a5070f51379e955ec493e22d082.
Linux deployment host, Debian 12 x86_64, Python 3.11.2, Docker Compose 5.5.1.
In an isolated source fixture: `PYTHONPATH=test/deployment python3 -m unittest test_blob_mounts -v`.
Both shared writable blob storage and pinned design revision contracts pass across every optional Compose profile. No services or runtime configuration were changed by these tests.
Production image compilation and post-deployment live validation remain separate checks. Simulation source is unchanged by this fix.
