#!/usr/bin/env bash
# Point apt at Ubuntu's HTTPS archive instead of a runner image's throttled
# Azure or EC2 regional mirror, and bound network stalls, keeping signature
# checks. The same rewrite as the inline bootstrap blocks, for steps that run
# after checkout. APT_ROOT (default /etc/apt) is for tests.
set -euo pipefail
root="${APT_ROOT:-/etc/apt}"
sudo_cmd=sudo
[ "${APT_ROOT:-}" ] && sudo_cmd=
for source in "$root"/sources.list "$root"/sources.list.d/*.list "$root"/sources.list.d/*.sources "$root"/apt-mirrors*.txt; do
  if [ -f "$source" ]; then
    $sudo_cmd sed -Ei 's#https?://(azure|[a-z0-9-]+\.ec2)\.archive\.ubuntu\.com/ubuntu#https://archive.ubuntu.com/ubuntu#g' "$source"
  fi
done
# No step installs from the runner image's Microsoft package source, and its
# signature lapses (403, "no longer signed") fail every apt-get update.
$sudo_cmd rm -f "$root"/sources.list.d/microsoft-prod.list "$root"/sources.list.d/microsoft-prod.sources
mkdir -p "$root/apt.conf.d" 2>/dev/null || true
$sudo_cmd tee "$root/apt.conf.d/80-ci-network" >/dev/null <<'CONF'
Acquire::By-Hash "force";
Acquire::Retries "3";
Acquire::http::Timeout "30";
Acquire::https::Timeout "30";
CONF
