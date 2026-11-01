#!/bin/bash
# Fails if a tracked file (or, with --staged, what's about to be committed) holds something
# private: a word from your own untracked list, or anything shaped like a secret.
#
#   tools/privacy_check.sh [--staged]
#
# The list is .private-words at the repo root (git-ignored; SNES9X_PRIVATE_WORDS points
# elsewhere): one word per line, compared without case; # starts a comment. Put your
# domain, email addresses and tunnel ids there. It is never committed.
set -u
root="$(git rev-parse --show-toplevel)" || exit 2
cd "$root"
cached=""
[ "${1:-}" = "--staged" ] && cached="--cached"
words="${SNES9X_PRIVATE_WORDS:-$root/.private-words}"
found=0

if [ -f "$words" ]; then
    list="$(mktemp)"
    grep -v '^[[:space:]]*#' "$words" | sed 's/[[:space:]]*$//' | grep -v '^$' > "$list"
    if [ -s "$list" ]; then
        if hits=$(git grep $cached -I -i -n -F -f "$list" -- . ':!.private-words'); then
            echo "privacy check: a private word is in a tracked file:" >&2
            echo "$hits" | cut -c1-200 >&2
            found=1
        fi
    fi
    rm -f "$list"
fi

# Shaped like a secret, whoever's it is.
patterns=(
    '"TunnelSecret"[[:space:]]*:'             # a Cloudflare tunnel's credentials file
    '"AccountTag"[[:space:]]*:'
    '-----BEGIN [A-Z ]*PRIVATE KEY-----'      # a key, or cloudflared's cert.pem
    '-----BEGIN ARGO TUNNEL TOKEN-----'
    '^RemoteToken=[A-Za-z0-9_-]{20,}'         # a remote control key from a settings file
    'NGROK_AUTHTOKEN=[A-Za-z0-9_]{20,}'
)
for p in "${patterns[@]}"; do
    if hits=$(git grep $cached -I -n -E -e "$p" -- . ':!tools/privacy_check.sh' ':!tests/remote/privacy_check_test.sh'); then
        echo "privacy check: something shaped like a secret:" >&2
        echo "$hits" | cut -c1-200 >&2
        found=1
    fi
done

[ $found -eq 0 ] || { echo "privacy check failed: take it out (or out of the index) before committing" >&2; exit 1; }
exit 0
