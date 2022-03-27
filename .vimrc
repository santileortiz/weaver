function! NoteLookup(value)
    mark Q
    cgetexpr system('./pymk.py search_notes --vim ''' . a:value . '''')
    copen
endfunction

:command! -nargs=1 Ns :call NoteLookup(<q-args>)
:command! Nn :execute ':e ' . system('./pymk.py new_note --vim')
:command! File :exe ':normal i' . system('./pymk.py file_store --vim') . ''
