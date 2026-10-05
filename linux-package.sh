# Consumers run without rebuilding; ship the generated WebP tree
# together with the executables that name it in their build config.
test -d build/linux/client/release/runtime-assets/data
{
  find build/linux/client/release build/sdl3-ci/prefix/lib -path '*/configure' -prune -o \( -type f -o -type l \) \( -perm /111 -o -name '*.so*' \) -print0
  find build/linux/client/release/runtime-assets -type f -print0
  printf '%s\0' build/linux/client/release/runtime-assets.json
} | tar --null -czf artifacts/ci-native/linux-test-programs.tar.gz -T -
