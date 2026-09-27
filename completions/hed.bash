# bash completion for hed  (SPDX-License-Identifier: MIT)
_hed() {
    local cur prev cmd
    cur="${COMP_WORDS[COMP_CWORD]}"; prev="${COMP_WORDS[COMP_CWORD-1]}"; cmd="${COMP_WORDS[1]}"
    local cmds="write show search replace insert delete spell langs config theme log edit help"
    case "$prev" in
        -l|--lang) COMPREPLY=($(compgen -W "python bash fish javascript typescript sql html css json markdown text" -- "$cur")); return;;
        --color) COMPREPLY=($(compgen -W "auto always never" -- "$cur")); return;;
        --theme) COMPREPLY=($(compgen -W "catppuccin dark light" -- "$cur")); return;;
        --indent) COMPREPLY=($(compgen -W "auto tabs spaces" -- "$cur")); return;;
    esac
    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=($(compgen -W "$cmds" -- "$cur") $(compgen -f -- "$cur")); return
    fi
    if [[ "$cur" == -* ]]; then
        local opts="--help --color --lang"
        case "$cmd" in
            write)   opts+=" --content --from --append --mode --parents --backup --no-clobber --strip-tabs --dedent --escapes --no-eol --diff --show --spell --quiet";;
            show)    opts+=" --number --range --lang --spell --color";;
            search)  opts+=" --regex --ignore-case --smart-case --word --context --count --files --max --json --fixed --line-number";;
            replace) opts+=" --old --new --old-file --new-file --all --nth --expect --regex --ignore-case --range --escapes --dry-run --backup --context --quiet --json";;
            insert)  opts+=" --content --from --at --before --after --start --end --after-match --before-match --first --match-indent --dedent --strip-tabs --escapes --dry-run --backup --quiet --json";;
            delete)  opts+=" --range --lines --match --regex --all --dry-run --backup --quiet --json";;
            spell)   opts+=" --lang --all-text --suggest --words --add --json --quiet";;
            config)  opts+=" --help";;
            log)     opts+=" --lines --help";;
            edit)    opts+=" --tabsize --tabs --spaces --no-spell --no-numbers --readonly --line --log-size";;
        esac
        COMPREPLY=($(compgen -W "$opts" -- "$cur")); return
    fi
    COMPREPLY=($(compgen -f -- "$cur"))
}
complete -o filenames -F _hed hed
