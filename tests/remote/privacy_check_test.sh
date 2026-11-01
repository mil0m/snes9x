#!/bin/bash
# The privacy check in a throwaway repo: a planted private word or secret fails it.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
git init -q .
mkdir tools
cp "$here/tools/privacy_check.sh" tools/
echo "example-private.test" > .private-words
echo "hello" > a.txt
git add a.txt tools
fail=0
expect() { # expect <pass|fail> <what> [--staged]
    local want=$1 what=$2; shift 2
    if tools/privacy_check.sh "$@" >/dev/null 2>&1; then got=pass; else got=fail; fi
    if [ "$got" = "$want" ]; then echo "ok   $what"; else echo "FAIL $what (it said $got)"; fail=1; fi
}
expect pass "a clean repo passes"
echo "see https://Example-Private.test/x" > b.txt; git add b.txt
expect fail "a private word fails it, whatever its case" --staged
git rm -q --cached b.txt; rm b.txt
expect pass "taken out of the index, it passes again" --staged
printf '[Corruption]\nRemoteToken=abcdefghijklmnopqrstuvwxyz012345\n' > c.conf; git add c.conf
expect fail "a remote key from a settings file fails it" --staged
git rm -q --cached c.conf; rm c.conf
printf '{"AccountTag":"x","TunnelSecret":"y"}\n' > creds.json; git add creds.json
expect fail "tunnel credentials fail it" --staged
git rm -q --cached creds.json; rm creds.json
echo "the plan mentions \"TunnelSecret\" and cert.pem in words" > plan.md; git add plan.md
expect pass "talking about them in prose is fine" --staged
exit $fail
