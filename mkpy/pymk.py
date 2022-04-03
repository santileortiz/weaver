# THIS IS NOT PYTHON CODE, it's BASH code installed into /usr/share/bash-completion/completions/
# to enable tab completions.
_pymk()
{
    local cur prev words cword

    #if OSX
    #  FIXME: Why is there no _init_completion in Brew's bash_complete?
    #  COMPREPLY=()
    #  _get_comp_words_by_ref cur prev words cword
    #else
    _init_completion || return

    res="$(./pymk.py --get_completions "$COMP_POINT $COMP_LINE")"
    completions=( $(compgen -W '$res' -- "$cur") )
    if [[ $completions ]]; then
        COMPREPLY=("${completions[@]}")
    else
        _filedir
    fi
} &&
complete -F _pymk "./pymk.py"

# ex: ts=4 sw=4 et filetype=sh
