--function! NoteLookup(value)
--    mark Q
--    cgetexpr system('./pymk.py search_notes --vim ''' . a:value . '''')
--    copen
--endfunction
--:command! -nargs=1 Ns :call NoteLookup(<q-args>)

local function open_new_note()
  vim.cmd("e " .. vim.fn.system('./pymk.py new_note --vim'))
end
vim.api.nvim_create_user_command('Nn', open_new_note, { nargs = 0 })

-- Enable concealing
vim.o.cole = 1


-- Open a file browser, chosen files will be uploaded to weaver and removed
-- from the source.
--
-- Each file will get a distinct identifier, they will be inserted into the
-- editor separated by commas.
local function files_insert()
  vim.cmd("normal a" .. vim.fn.system('./pymk.py file_store --vim'))
end
vim.api.nvim_create_user_command('Files', files_insert, { nargs = 0 })

-- Open a file browser, chosen files will be uploaded to weaver and removed
-- from the source.
--
-- Only one identifier is created and inserted to the editor. If multiple files
-- were selected, they will be added to the same file sequence corresponding to
-- the new identifier.
local function file_sequence_insert()
  vim.cmd("normal a" .. vim.fn.system('./pymk.py file_store --vim --sequence'))
end
vim.api.nvim_create_user_command('FileSequence', file_sequence_insert, { nargs = 0 })

-- No identifier is created, will use the one in the filename of the active
-- buffer. This doesn't add anything into the page because files are implicitly
-- linked by having the same ID. Execution fails if there's already a file with
-- that ID.
local function file_attach()
  vim.cmd('echo "' .. vim.fn.system('./pymk.py file_store --vim --attach "' .. vim.fn.expand("%:p") .. '"'))
end
vim.api.nvim_create_user_command('FileAttach', file_attach, { nargs = 0 })


-- Receives as parameter a type, the correct file for the type's data is opened
-- and a new instance is appended to it (presumably using the default
-- constructor?). The ID of the new instance is inserted where the current
-- cursor is placed.
-- :command! Instance

-- Expects and ID to be highlighted (maybe later a simple query? like
-- q("John")). Opens a file browser and the chosen files are appended to the
-- corresponding instance.
--
-- There are still some undefined semantics like, are files grouped into the
-- instance's identifier?, are distinct IDs created and then added to the file
-- property?. What happens if the instance has no ID?. What happens if they
-- already have a file property and user specifies images to be grouped? this
-- would make a file have both ID implicit files, and explicitly assigned
-- files... which is probably not an issue, but depends on how other stuff is
-- implemented.
--
-- TODO: Implement it...
-- :command! FilesAppend


-- Identical to :Files command, but wraps inserted text with \image{}.
-- TODO: This probably isn't that useful, haven't used it a lot.
local function image()
  vim.cmd('normal a\\image{' .. vim.fn.system('./pymk.py file_store --vim'))
end
vim.api.nvim_create_user_command('Image', image, { nargs = 0 })

-- Automatic query for ID under cursor to view associated files
local function view()
  vim.fn.system('./pymk.py view --vim ' .. vim.fn.expand("<cword>"))
end
vim.keymap.set('n', '<leader>v', view, { desc = '[V]iew Files' })

vim.keymap.set('v', '`', 'c`<ESC>pa`<ESC>')

local wrap_brace = 'c{<ESC>pa}<ESC>'
vim.keymap.set('v', '{', wrap_brace)
vim.keymap.set('v', '}', wrap_brace)

local wrap_bracket = 'c[<ESC>pa]<ESC>vi]'
vim.keymap.set('v', '[', wrap_bracket, {remap = false})
vim.keymap.set('v', ']', wrap_bracket, {remap = false})

local wrap_parenthesis = 'c(<ESC>pa)<ESC>vi'
vim.keymap.set('v', '(', wrap_parenthesis)
vim.keymap.set('v', ')', wrap_parenthesis)

-- i alone is used as start of inner selection commands iw, i(, i[ etc. Avoid
-- conflict with leader as prefix.
vim.keymap.set('v', '<leader>i', 'c\\i{<ESC>pa}<ESC>')
vim.keymap.set('v', '<leader>b', 'c\\b{<ESC>pa}<ESC>')
