#!/bin/bash
# The engine boundary check on a throwaway engine folder: a core header or a hand-declared
# core global outside the glue files fails it; the glue files and bundled libraries pass.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/e"
printf '#pragma once\n' > "$tmp/e/rule.h"
printf '#include "rule.h"\n#include "nlohmann/json.hpp"\n#include <vector>\n' > "$tmp/e/rule.cpp"
printf '#include "core_access.h"\n#include "snes9x.h"\n#include "memmap.h"\n' > "$tmp/e/core_access.cpp"
printf '#pragma once\n' > "$tmp/e/core_access.h"
printf 'extern void (*S9xCorruptFrameHook)(void);\n' > "$tmp/e/hook.h"
fail=0
expect() { # expect <pass|fail> <what>
    local want=$1 what=$2
    if "$here/tools/engine_boundary_check.sh" "$tmp/e" >/dev/null 2>&1; then got=pass; else got=fail; fi
    if [ "$got" = "$want" ]; then echo "ok   $what"; else echo "FAIL $what (it said $got)"; fail=1; fi
}
expect pass "the glue files may reach the core; engine headers and bundled libraries are fine"
printf '#include "rule.h"\n#include "ppu.h"\n' > "$tmp/e/paint.cpp"
expect fail "a core header in an engine file fails"
printf '#include "rule.h"\n' > "$tmp/e/paint.cpp"
expect pass "and passes again once it's gone"
printf 'extern struct SCPUState CPU;\n' > "$tmp/e/paint.cpp"
expect fail "a core global declared by hand fails"
rm "$tmp/e/paint.cpp"
printf '#include "../snes9x.h"\n' > "$tmp/e/journal.h"
expect fail "a core header by relative path fails"
exit $fail
