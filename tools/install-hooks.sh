#!/bin/bash
# Makes tools/privacy_check.sh this clone's pre-commit hook.
set -eu
root="$(git rev-parse --show-toplevel)"
hook="$(git rev-parse --git-path hooks)/pre-commit"
cat > "$hook" <<'HOOK'
#!/bin/bash
exec "$(git rev-parse --show-toplevel)/tools/privacy_check.sh" --staged
HOOK
chmod +x "$hook" "$root/tools/privacy_check.sh"
echo "pre-commit hook: $hook"
[ -f "$root/.private-words" ] || echo "add your private words (domain, emails, tunnel ids) to $root/.private-words"
