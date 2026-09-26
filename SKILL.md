---
name: hed
description: Create, inspect and edit files from the shell with `hed` instead of heredocs, sed, or cat. Use whenever you need to write a file, view numbered lines, make an exact-text replacement, insert/delete lines, search, or spell-check prose/comments. Works identically in bash, zsh and fish (fish has no heredocs).
---

# hed — file editing for agents

`hed` one-shot commands never prompt, never open an editor, write atomically, and
print a unified diff of every edit so you can verify the result without re-reading
the file. Exit codes: `0` ok, `1` not found / misspellings, `2` usage or I/O error,
`3` ambiguous match (nothing written).

## Standard workflow
1. **Look** before editing: `hed show FILE -n -r 40:80` (tab-separated line numbers; ranges `A:B`, `A:+N`, `-20:`).
2. **Find**: `hed search 'text' FILE` → `LINE:COL:text`. Add `--json`, `-E` (regex), `-i`, `-C 2`.
3. **Edit** with the smallest unique anchor (see below).
4. **Read the diff** hed prints. If the exit code is not 0, nothing was written — read stderr, fix, retry.

## Writing files (replaces `cat > f <<'EOF'`)
```bash
hed write path/app.py <<'EOF'        # bash/zsh heredoc into hed
...content...
EOF

hed write -p -m 755 bin/run.sh -d -c '
    #!/usr/bin/env bash
    echo "literal $vars are NOT expanded"
'                                     # bash AND fish: single-quoted multi-line + --dedent

printf '%s\n' "$data" | hed write out.txt
```
`-p` make parent dirs · `-m` octal mode · `-a` append · `-b` timestamped backup ·
`-n` refuse to overwrite · `-D` diff vs previous · `--spell` warn on typos.
Content is literal (like `<<'EOF'`). A trailing newline is added unless `--no-eol`.
In fish, a literal `'` inside single quotes is written `\'`; for content with many
quotes, pipe it in or use `--from FILE`.

## Replacing text (the main editing tool)
```bash
hed replace FILE 'exact old text' 'new text'
```
* OLD must match **exactly once**. On exit 3 hed lists the matching lines: widen OLD
  with neighbouring text (multi-line OLD is fine), or use `--nth N` / `--all`.
* On exit 1 hed says whether a match exists when whitespace is ignored and prints the
  `hed show` command to copy the exact indentation.
* Multi-line / quote-heavy text: `--old-file old.txt --new-file new.txt` (`-` = stdin).
* Safety: `--expect N` (fail unless exactly N replacements), `-n` dry run, `-b` backup.
* Regex: `-E 'foo_(\d+)' 'bar_$1'` (`^`/`$` are per line). Delete text: NEW = `''`.
* CRLF files are handled for you; always use `\n` line breaks in OLD/NEW.

## Inserting / deleting lines
```bash
hed insert FILE --after-match 'import os' -c 'import sys'
hed insert FILE --at 10 -I < block.txt   # -I re-indents the block to match line 10
hed insert FILE --end -c 'last line'
hed delete FILE --range 12:15            # or --match 'TODO remove' [--all]
```
Positions: `--at N`, `--before N`, `--after N` (0 = top), `--start`, `--end`,
`--after-match TEXT`, `--before-match TEXT` (must be unique unless `--first`).

## Spell-checking
`hed spell FILE [--json]` — code files: only comments & string literals; Markdown/
text/HTML: all prose. Identifiers, paths, URLs, ACRONYMS and camelCase are skipped.
Add project words: `hed spell --add kubeconfig --add homelab` (~/.config/hed/words.txt).

## Editor config & status log
The interactive editor reads settings from `~/.config/hed/config` (tab size, indent,
spell, line numbers, syntax theme, log size). Query or set them without opening the
editor:
```bash
hed config                 # print the config path and effective settings
hed config get theme       # catppuccin | dark | light
hed config set theme dark  # set and save
hed log 20                 # last 20 status messages the editor logged
```
Themes: `catppuccin`, `dark`, `light`. The editor also supports mouse (click to move
cursor, wheel to scroll), crash recovery via swap files (`~/.config/hed/swap/`), and
live search/replace feedback (`match X of Y`). `hed langs --help` lists languages.

## Rules of thumb
* Prefer `hed replace` over rewriting whole files; prefer `hed write` over heredocs.
* Quote OLD/NEW with single quotes. Don't use `sed -i` for code edits.
* Pass `--color=never` if you parse output (auto mode already disables color when piped).
* `hed langs` lists highlight languages; `-l LANG` overrides detection.
* Never run bare `hed FILE` / `hed edit` as an agent — that's the interactive editor
  for humans (it exits 2 when there is no terminal).
