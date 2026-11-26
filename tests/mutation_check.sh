#!/bin/sh
# 뮤테이션 테스트: 코드를 복사본에서 일부러 고쳐, 기존 테스트가 잡는지 확인한다.
# 원본 워킹트리는 건드리지 않는다.
set -u
ORIG=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/jg_mutation_XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cp -R "$ORIG/Makefile" "$ORIG/include" "$ORIG/src" "$ORIG/tests" "$ORIG/www" "$ORIG/conf" "$TMP/"
cd "$TMP"

fail=0
mutate() {
    name=$1; file=$2; old=$3; new=$4
    cp "$ORIG/$file" "$TMP/$file"
    python3 - "$file" "$old" "$new" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path, encoding='utf-8').read()
if old not in text:
    raise SystemExit('mutation target not found: ' + path)
open(path, 'w', encoding='utf-8').write(text.replace(old, new, 1))
PY
    make clean >/dev/null 2>&1
    if ! make >/dev/null 2>&1; then
        echo "FAIL  $name: mutant did not build"
        fail=1
        return
    fi
    if make test >/dev/null 2>&1; then
        echo "FAIL  $name: mutant survived (tests passed)"
        fail=1
    else
        echo "PASS  $name: mutant killed"
    fi
}

echo '=== mutation tests ==='
mutate 'unsupported method check' \
    'src/http_parser.c' \
    'if (!is_get && !is_post) return fail(req, 405);' \
    'if (is_get && is_post) return fail(req, 405);'

mutate 'doc_root boundary check' \
    'src/static_handler.c' \
    '    if (!inside) return err(403);' \
    '    if (inside) return err(403);'

[ $fail -eq 0 ] && echo 'mutation tests: all killed' || { echo 'mutation tests: FAILED'; exit 1; }
