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

:vnoremap { c{<ESC>pa}<ESC>
:vnoremap } c{<ESC>pa}<ESC>

:vnoremap [ c[<ESC>pa]<ESC>
:vnoremap ] c[<ESC>pa]<ESC>

:vnoremap ( c(<ESC>pa)<ESC>
:vnoremap ) c(<ESC>pa)<ESC>

:vnoremap i c\i{<ESC>pa}<ESC>
:vnoremap b c\b{<ESC>pa}<ESC>
