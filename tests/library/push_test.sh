#!/bin/bash
# tools/library/push.sh against a throwaway remote: commits a session's files, skips sync
# conflict copies, says when there's nothing new. Prints FAIL lines on failure.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/push-test.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
fail() { echo "FAIL: $*"; status=1; }
status=0
git init -q --bare "$tmp/remote.git"
git clone -q "$tmp/remote.git" "$tmp/lib" 2>/dev/null
cd "$tmp/lib"
git config user.name tester; git config user.email tester@example.invalid
mkdir -p tools "GOOF TROOP [2bb368c4]/recipes"
cp "$here/tools/library/push.sh" tools/push.sh
printf '*.sync-conflict-*\n' > .gitignore
echo '{}' > "GOOF TROOP [2bb368c4]/calls.erik.json"
echo '{}' > "GOOF TROOP [2bb368c4]/recipes/moon.json"
echo '{}' > "GOOF TROOP [2bb368c4]/recipes/moon.sync-conflict-20261102-201512-ABCDEFG.json"
out=$(tools/push.sh 2>&1) || fail "push.sh failed: $out"
echo "$out" | grep -q "moon.sync-conflict" || fail "conflict copy not listed: $out"
echo "$out" | grep -q "^Pushed:" || fail "nothing pushed: $out"
msg=$(git --git-dir="$tmp/remote.git" log -1 --format=%s)
case "$msg" in "Session "*": GOOF TROOP") ;; *) fail "commit message: $msg" ;; esac
git --git-dir="$tmp/remote.git" ls-tree -r --name-only HEAD | grep -q sync-conflict && fail "conflict copy was pushed"
out=$(tools/push.sh 2>&1)
echo "$out" | grep -q "Nothing new" || fail "second run: $out"
[ $status -eq 0 ] && echo "push_test: ok"
exit $status
