function! NoteLookup(value)
    mark Q
    cgetexpr system('./pymk.py search_notes --vim ''' . a:value . '''')
    copen
endfunction

" Enable concealing
set cole=1

:command! -nargs=1 Ns :call NoteLookup(<q-args>)
:command! Nn :execute ':e ' . system('./pymk.py new_note --vim')

" Open a file browser, chosen files will be uploaded to weaver and removed
" from the source.
"
" Each file will get a distinct identifier, they will be inserted into the
" editor separated by commas.
:command! Files :exe ':normal a' . system('./pymk.py file_store --vim') . ''

" Open a file browser, chosen files will be uploaded to weaver and removed
" from the source.
"
" Only one identifier is created and inserted to the editor. If multiple files
" were selected, they will be added to the same file sequence corresponding to
" the new identifier.
:command! FileSequence :exe ':normal a' . system('./pymk.py file_store --vim --sequence') . ''

" No identifier is created, will use the one in the filename of the active
" buffer. This doesn't add anything into the page because files are implicitly
" linked by having the same ID. Execution fails if there's already a file with
" that ID.
:command! FileAttach :exe ':echo "' . system('./pymk.py file_store --vim --attach "' . expand("%:p") . '"') . '"'

" Receives as parameter a type, the correct file for the type's data is opened
" and a new instance is appended to it (presumably using the default
" constructor?). The ID of the new instance is inserted where the current
" cursor is placed.
" :command! Instance

" Expects and ID to be highlighted (maybe later a simple query? like
" q("John")). Opens a file browser and the chosen files are appended to the
" corresponding instance.
"
" There are still some undefined semantics like, are files grouped into the
" instance's identifier?, are distinct IDs created and then added to the file
" property?. What happens if the instance has no ID?. What happens if they
" already have a file property and user specifies images to be grouped? this
" would make a file have both ID implicit files, and explicitly assigned
" files... which is probably not an issue, but depends on how other stuff is
" implemented.
"
" TODO: Implement it...
" :command! FilesAppend


" Identical to :Files command, but wraps inserted text with \image{}.
" TODO: This probably isn't that useful, haven't used it a lot.
:command! Image :exe ':normal a\image{' . system('./pymk.py file_store --vim') . '}'

" Automatic query for ID under cursor to view associated files
:command! View silent! :exe system('./pymk.py view --vim ' . expand("<cword>"))
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
