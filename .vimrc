function! NoteLookup(value)
    mark Q
    cgetexpr system('./pymk.py search_notes --vim ''' . a:value . '''')
    copen
endfunction

" Enable concealing
set cole=1

:command! -nargs=1 Ns :call NoteLookup(<q-args>)
:command! Nn :execute ':e ' . system('./pymk.py new_note --vim')
:command! File :exe ':normal a[[' . system('./pymk.py file_store --vim') . ']]'
:command! Image :exe ':normal a\image{' . system('./pymk.py file_store --vim') . '}'

" Automatic query for ID under cursor to view associated files
:command! View silent! :exe system('./pymk.py view ' . expand("<cword>"))
:noremap <leader>v :View<CR>

:vnoremap ` c`<ESC>pa`<ESC>

:vnoremap { c{<ESC>pa}<ESC>vi}
:vnoremap } c{<ESC>pa}<ESC>vi}

:vnoremap [ c[<ESC>pa]<ESC>vi]
:vnoremap ] c[<ESC>pa]<ESC>vi]

:vnoremap ( c(<ESC>pa)<ESC>vi)
:vnoremap ) c(<ESC>pa)<ESC>vi)

" i alone is used as start of inner selection commands iw, i(, i[ etc. Avoid
" conflict with leader as prefix.
:vnoremap <leader>i c\i{<ESC>pa}<ESC>
:vnoremap <leader>b c\b{<ESC>pa}<ESC>
