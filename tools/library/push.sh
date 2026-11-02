#!/bin/bash
# Puts what a play session added to the shared library into git and pushes it (#62).
# Lives in the library repo as tools/push.sh; run it where the git clone is (the server):
#   ssh <server> '<library>/tools/push.sh'
# Commits as whoever the clone's git config says. Conflict copies a sync left are skipped
# (.gitignore) and listed, so someone can look at them.
set -euo pipefail
cd "$(dirname "$0")/.."
conflicts=$(find . -name '*.sync-conflict-*' -not -path './.git/*' | sed 's|^\./||')
if [ -n "$conflicts" ]; then
    echo "Two versions kept by the sync (not pushed; look and delete the one you don't want):"
    echo "$conflicts" | sed 's/^/  /'
fi
git add -A
if git diff --cached --quiet; then
    echo "Nothing new to push."
    exit 0
fi
# The games touched: folders named "<title> [<hash>]".
games=$(git diff --cached --name-only | awk -F/ 'NF > 1 {print $1}' | grep -E ' \[[0-9a-f]+\]$' | sed -E 's/ \[[0-9a-f]+\]$//' |
    sort -u | paste -sd, - | sed 's/,/, /g' || true)
git commit -q -m "Session $(date +%Y-%m-%d): ${games:-library}"
git push -q
echo "Pushed: $(git log --oneline -1)"
