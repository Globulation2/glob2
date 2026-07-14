# Removing Unused Includes

Pipeline for stripping genuinely-unused `#include`s from `src/`, behavior-preserving. First run 2026-07-13 removed 891 across 182 files with byte-identical replays.

Tooling is `clang-tidy misc-include-cleaner` (Homebrew LLVM, `/opt/homebrew/opt/llvm/bin/`) driven off the existing `compile_commands.json`. All source edits are made by clang tooling (`clang-apply-replacements`), never by `sed`/`awk`.

## 1. Detect

```bash
cd glob2
SDK=$(xcrun --show-sdk-path)
/opt/homebrew/opt/llvm/bin/run-clang-tidy \
  -p . -clang-tidy-binary /opt/homebrew/opt/llvm/bin/clang-tidy \
  -checks='-*,misc-include-cleaner' \
  -extra-arg-before=-isysroot -extra-arg-before="$SDK" \
  -j 8 '/src/.*\.(cpp|cxx|cc)$'
```

- **macOS gotcha:** the `-isysroot $(xcrun --show-sdk-path)` args are mandatory. Without them every file fails with `'vector' file not found` and the analysis is silently useless.
- Give `-j` an explicit number — `$(sysctl -n hw.ncpu)` comes back empty under the sandbox and breaks the run.
- `misc-include-cleaner` is analyzed per translation unit, so it only reports on `.cpp` main files, never headers.

## 2. Removal-only fix-its

The check emits **two** diagnostic directions:

- `included header X is not used directly` → a removal fix-it. **This is what we want.**
- `no header providing Y is directly included` → an insertion fix-it (IWYU-style "add the direct include"). **Skip it** — adding direct includes is churn with no porting benefit.

`clang-tidy --fix` applies both. To get removal-only: `--export-fixes` to YAML, keep only diagnostics whose message contains `is not used directly`, then apply with `clang-apply-replacements`.

## 3. Compile-gate every removal (essential)

Removal-only can still break the **transitive case**: a flagged header is the sole provider of a symbol the file uses, so IWYU flags it as "not used directly" yet deleting it fails to compile. Gate each file:

1. Apply the file's removal fix-its.
2. Syntax-check with that file's own flags: reuse its `compile_commands.json` command with `-o` stripped and ` -c ` replaced by ` -fsyntax-only -isysroot $SDK ` (writes no object).
3. If it fails, revert (`git checkout -- <file>`).

For a file whose full removal set breaks, retry per-include **bottom-up** (descending source offset, so unprocessed offsets stay valid), snapshotting before each apply and restoring on failure so earlier kept removals survive. This recovers the redundant includes while keeping the one load-bearing header.

## 4. Verify

- **Default build:** `scons -j16`.
- **Server build:** `scons server=1 -j16` — catches includes used only under `YOG_SERVER_ONLY`, which are invisible to the default-config analysis. (`ar` needs the sandbox disabled to write its temp file.)
- **Determinism:** do **not** trust a byte-diff against `tests/baselines/cpp-refactor.replay` — that baseline goes stale, and run-to-completion game *length* is behavior-dependent, so both size and bytes drift for unrelated reasons. Instead run a same-binary A/B at a **fixed tick count**:

  ```bash
  git worktree add --detach /tmp/glob2-base HEAD   # baseline = tree without the removals
  (cd /tmp/glob2-base && scons release=1 -j16)
  for g in G2 gd-small-2ai gd-large-4ai gd-archipelago gd-bigarena-long; do
    /tmp/glob2-base/build/src/glob2 --nox games/$g.game 20000 1 && cp ~/.glob2/replays/last_game.replay /tmp/B.replay
    ./build/src/glob2               --nox games/$g.game 20000 1 && cp ~/.glob2/replays/last_game.replay /tmp/A.replay
    cmp /tmp/A.replay /tmp/B.replay && echo "$g EQUAL" || echo "$g DIFFERS"
  done
  git worktree remove /tmp/glob2-base
  ```

  Byte-equal at a fixed tick count = behavior preserved. This is the only check that catches the real risk of an unused-include removal: losing a removed header's static-initializer side effect. Replay runs need the sandbox disabled (engine writes to `~/.glob2/`).

## Scope

`src/` only. `libgag/` and `libusl/` are vendored and out of scope for cleanup (see `cpp-bugs/CLAUDE.md`).
