function! NoteLookup(value)
    mark Q
    cgetexpr system('./pymk.py search_notes --vim ''' . a:value . '''')
    copen
endfunction

:command! -nargs=1 Ns :call NoteLookup(<q-args>)
:command! Nn :execute ':e ' . system('./pymk.py new_note --vim')
:command! File :exe ':normal i[[' . system('./pymk.py file_store --vim') . ']]'
:command! Image :exe ':normal i\image{' . system('./pymk.py file_store --vim') . '}'

:vnoremap ` c`<ESC>pa`<ESC>

:vnoremap { c{<ESC>pa}<ESC>vi}
:vnoremap } c{<ESC>pa}<ESC>vi}

:vnoremap [ c[<ESC>pa]<ESC>vi]
:vnoremap ] c[<ESC>pa]<ESC>vi]

:vnoremap ( c(<ESC>pa)<ESC>vi)
:vnoremap ) c(<ESC>pa)<ESC>vi)

" i alone is used as start of inner selection commands iw, i(, i[ etc. Avoid
" conflict with t prefix as in text.
:vnoremap ti c\i{<ESC>pa}<ESC>
:vnoremap tb c\b{<ESC>pa}<ESC>
