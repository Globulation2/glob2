# Run from repository root at tested source SHA; existing documented test PostgreSQL must be available.
npm run test --prefix platform -- packages/music-studio packages/map-studio packages/billing packages/music packages/db/test/schema.test.ts packages/protocol/test/fixtures.test.ts apps/ai-music-worker/test/pipeline.test.ts apps/ai-music-worker/test/runner.test.ts apps/api/test/musicStudio.test.ts apps/api/test/music.test.ts apps/api/test/accountExport.test.ts apps/api/test/studioEvents.test.ts apps/api/test/studio.test.ts apps/worker/test/reliability.test.ts apps/web/test/musicStudio.test.tsx apps/web/test/musicStudioDraft.test.tsx apps/web/test/musicStudioDraft.test.ts apps/web/test/musicStudioStream.test.tsx apps/web/test/musicPlayer.test.tsx apps/web/test/musicPlayback.test.ts apps/web/test/musicCatalogue.test.tsx
npm run typecheck --prefix platform
npm run lint --prefix platform
PYTHONPATH=tools/music tools/music/.venv/bin/python -m unittest discover -s tools/music/tests
python3 -m unittest discover -s test/build_system -p 'test_ci*.py'
python3 -m unittest discover -s test/deployment -p 'test_online_deploy.py'

# Native TypeScript needs Node 24; task-specific variable preserves system environment.
MUSIC_NODE24=/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39/node/24.19.0_64bit/bin
PATH="$MUSIC_NODE24:$PATH" npm exec --prefix platform --workspace @glob2/web -- vite build
PATH="$MUSIC_NODE24:$PATH" SCREENSHOT_DIR="$PWD/artifacts/music-studio-reviewed-visual" npm run --prefix platform --workspace @glob2/web e2e -- music-studio.spec.ts

docker build --target ai-music-worker -f deploy/Dockerfile -t glob2-ai-music-worker:reviewed .
docker run --rm --network none --memory=12g --cpus=2 --pids-limit=96 --read-only --tmpfs /tmp:size=128m,mode=1777 -e HOME=/tmp -e OPENBLAS_NUM_THREADS=1 -e OMP_NUM_THREADS=1 glob2-ai-music-worker:reviewed /opt/music/bin/python -m glob2music.studio probe --job /tmp --cache /opt/music-assets

# Existing writable mountpoint /tmp/glob2-music-test-mount; test identity uid/gid 10001.
systemd-run --user --scope -p MemoryMax=12G unshare --user --map-root-user --mount sh -c 'mount -t tmpfs -o size=6g tmpfs /tmp/glob2-music-test-mount && unshare --user --map-user=10001 --map-group=10001 sh -c "MUSIC_SCRATCH=/tmp/glob2-music-test-mount MUSIC_SANDBOX_TEST=1 npm run test --prefix platform -- apps/ai-music-worker/test/runner.test.ts"'
# Copy included render scripts into artifacts/ to reproduce their repository-relative imports.
systemd-run --user --scope -p MemoryMax=12G unshare --user --map-root-user --mount sh -c 'mount -t tmpfs -o size=6g tmpfs /tmp/glob2-music-test-mount && unshare --user --map-user=10001 --map-group=10001 /home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39/node/24.19.0_64bit/bin/node artifacts/music-studio-review-acoustic-final.ts'
systemd-run --user --scope -p MemoryMax=12G unshare --user --map-root-user --mount sh -c 'mount -t tmpfs -o size=6g tmpfs /tmp/glob2-music-test-mount && unshare --user --map-user=10001 --map-group=10001 /home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39/node/24.19.0_64bit/bin/node artifacts/music-studio-review-synth-revision-2.ts'
# This host's default Docker policy must fail closed, even with a valid memory bound.
docker run --rm --network none --memory=12g --cpus=2 --pids-limit=96 --read-only --tmpfs /tmp:size=6g,mode=1777 glob2-ai-music-worker:reviewed node --input-type=module -e "import {createRunner} from './apps/ai-music-worker/src/runner.ts'; try {await createRunner('/app/tools/music','/opt/music-assets','/tmp','/opt/music/bin/python'); throw Error('Expected this host policy to deny nested namespaces');} catch(e) {if (!/Operation not permitted|No permissions to create new namespace/.test(String(e))) throw e; console.log('PASS: bounded container passes memory checks and default host policy rejects namespace creation; no jobs accepted.');}"
