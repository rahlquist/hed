#!/usr/bin/env bash
# hed test suite (SPDX-License-Identifier: MIT)
set -u
H="${HED:-$(cd "$(dirname "$0")/.." && pwd)/hed}"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cd "$T"
export HOME="$T"   # isolate config/themes so tests never touch the real ~/.config/hed
pass=0; failn=0
ok()  { pass=$((pass+1)); }
bad() { failn=$((failn+1)); echo "FAIL: $*"; }
check() { local name=$1; shift; if "$@"; then ok; else bad "$name"; fi; }

$H write a.py -q <<'X'
def f():
    return 1
X
check "write heredoc"  test "$(cat a.py)" = $'def f():\n    return 1'
$H write b.txt -q -d -c '
    one
      two
'
check "dedent"         test "$(cat b.txt)" = $'one\n  two'
printf 'x' | $H write c.txt -q;              check "eol added" test "$(od -An -c c.txt | tr -d ' ')" = 'x\n'
printf 'x' | $H write c.txt -q --no-eol;     check "no-eol"    test "$(wc -c < c.txt)" = 1
$H write d.txt -q -a -c 1; $H write d.txt -q -a -c 2; check "append" test "$(cat d.txt)" = $'1\n2'
$H write e/f/g.txt -q -p -m 600 -c hi;       check "parents+mode" test "$(stat -c %a e/f/g.txt)" = 600
$H write a.py -q -n -c x 2>/dev/null; check "no-clobber rc" test $? -eq 1
$H write a.py -q -e -c 'l1\nl2'; check "escapes" test "$(cat a.py)" = $'l1\nl2'
$H write a.py -q -c 'aa bb aa'
$H replace a.py aa cc -q 2>/dev/null; check "ambiguous rc=3" test $? -eq 3
$H replace a.py 'bb' 'BB' -q;               check "unique replace" test "$(cat a.py)" = 'aa BB aa'
$H replace a.py aa zz --all -q;              check "replace all" test "$(cat a.py)" = 'zz BB zz'
$H replace a.py nope x -q 2>/dev/null;       check "no match rc=1" test $? -eq 1
$H replace a.py 'z+' 'Q' -E --all -q;        check "regex" test "$(cat a.py)" = 'Q BB Q'
$H replace a.py BB XX --expect 2 -q 2>/dev/null; check "expect mismatch" test $? -eq 3
$H replace a.py BB XX -n -q;                 check "dry run" test "$(cat a.py)" = 'Q BB Q'
printf 'a\r\nb\r\n' > crlf.txt; $H replace crlf.txt $'a\nb' 'c' -q; check "crlf kept" test "$(od -An -c crlf.txt | tr -d ' ')" = 'c\r\n'
printf '1\n2\n3\n' > n.txt
$H insert n.txt --after 1 -c X -q;           check "insert after" test "$(cat n.txt)" = $'1\nX\n2\n3'
$H insert n.txt --end -c E -q;               check "insert end" test "$(tail -1 n.txt)" = E
$H insert n.txt --at 1 -c S -q;              check "insert at 1" test "$(head -1 n.txt)" = S
$H delete n.txt --range 1:2 -q;              check "delete range" test "$(cat n.txt)" = $'X\n2\n3\nE'
$H delete n.txt --match E -q;                check "delete match" test "$(cat n.txt)" = $'X\n2\n3'
check "show range"     test "$($H show n.txt -r 2:3 --color=never)" = $'2\n3'
check "show numbers"   test "$($H show n.txt -n -r -1: --color=never)" = $'3\t3'
$H search 2 n.txt >/dev/null; check "search hit rc" test $? -eq 0
$H search zzz n.txt >/dev/null;              check "search miss rc" test $? -eq 1
check "search json"    bash -c "$H search 3 n.txt --json | grep -q '\"line\":3'"
printf 'This has teh typo\n' > s.md
$H spell s.md >/dev/null;                    check "spell finds" test $? -eq 1
check "spell suggests" bash -c "$H spell s.md | grep -q 'teh -> the'"
printf 'x = 1  # a correct comment\n' > ok.py
$H spell ok.py >/dev/null;                   check "spell clean" test $? -eq 0
printf 'getUserName = "https://x.io/path" # HTTP API\n' > code.py
$H spell code.py >/dev/null;                 check "spell skips code-ish" test $? -eq 0
check "stdin show"     test "$(echo hi | $H show --color=never)" = hi
if command -v fish >/dev/null; then
  fish -c "$H write fish.txt -q -d -c '
      from fish
  '"
  check "fish write" test "$(cat fish.txt)" = 'from fish'
fi

# custom themes (HOME is isolated above, so this never touches the real config)
$H theme create mytest >/dev/null
check "theme create"   test -f "$HOME/.config/hed/themes/mytest.theme"
check "theme lists custom" bash -c "$H theme | grep -q '^  mytest'"
check "theme template keys" bash -c "grep -q '^keyword = ' \"$HOME/.config/hed/themes/mytest.theme\""
$H config set theme mytest >/dev/null 2>&1
check "config set accepts custom" test "$($H config get theme)" = mytest
$H config set theme nope 2>/dev/null; check "config set rejects unknown" test $? -eq 2
check "HED_THEME custom" test "$(HED_THEME=mytest $H config get theme)" = mytest
$H theme create catppuccin >/dev/null 2>&1   # allowed: overrides the built-in
check "override builtin allowed" test -f "$HOME/.config/hed/themes/catppuccin.theme"
check "override listed as custom" bash -c "$H theme | grep -q 'overrides built-in'"

echo "passed: $pass  failed: $failn"
[ $failn -eq 0 ]
