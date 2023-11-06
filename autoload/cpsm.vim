" cpsm - fuzzy path matcher
" Copyright (C) 2015 the Authors
"
" Licensed under the Apache License, Version 2.0 (the "License");
" you may not use this file except in compliance with the License.
" You may obtain a copy of the License at
"
"     http://www.apache.org/licenses/LICENSE-2.0
"
" Unless required by applicable law or agreed to in writing, software
" distributed under the License is distributed on an "AS IS" BASIS,
" WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
" See the License for the specific language governing permissions and
" limitations under the License.

" Global variables and defaults
if !exists('g:cpsm_highlight_mode')
  let g:cpsm_highlight_mode = 'detailed'
en
if !exists('g:cpsm_match_empty_query')
  let g:cpsm_match_empty_query = 1
en
if !exists('g:cpsm_max_threads')
  if has('win32unix')
    " Synchronization primitives are extremely slow on Cygwin:
    " https://cygwin.com/ml/cygwin/2012-08/msg00200.html
    let g:cpsm_max_threads = 1
  el
    let g:cpsm_max_threads = 0
  en
en
if !exists('g:cpsm_query_inverting_delimiter')
  let g:cpsm_query_inverting_delimiter = ''
en
if !exists('g:cpsm_unicode')
  let g:cpsm_unicode = 0
en

let s:script_dir = escape(expand('<sfile>:p:h'), '\')

fu! s:CtrlPMatchC(items, str, limit, mmode, ispath, crfile, regex) abort
  if s:status == 0
    retu ['ERROR: init failed to load cpsm module']
  elsei s:status == 1
    retu ['ERROR: failed to load cpsm module']
  en

  if empty(a:str) && g:cpsm_match_empty_query == 0
    let s:results = a:items[0:(a:limit)]
    let s:regexes = []
  el
    let s:match_crfile = exists('g:ctrlp_match_current_file') ? g:ctrlp_match_current_file : 0
    let s:regex_line_prefix = '> '
    if exists('g:ctrlp_line_prefix')
      let s:regex_line_prefix = g:ctrlp_line_prefix
    en
    let s:input = {
    \   'limit': a:limit,
    \   'mmode': a:mmode,
    \   'ispath': a:ispath,
    \   'crfile': a:crfile,
    \   'highlight_mode': g:cpsm_highlight_mode,
    \   'match_crfile': s:match_crfile,
    \   'max_threads': g:cpsm_max_threads,
    \   'query_inverting_delimiter': g:cpsm_query_inverting_delimiter,
    \   'regex_line_prefix': s:regex_line_prefix,
    \   'unicode': g:cpsm_unicode,
    \ }

    let s:output = cpsm_ctrlp_match(a:items, a:str, s:input)
    let s:results = s:output[0]
    let s:regexes = s:output[1]
  en

  cal clearmatches()
  " Apply highlight regexes.
  for r in s:regexes
    cal matchadd('CtrlPMatch', r)
  endfo
  " CtrlP does this match to hide the leading > in results.
  cal matchadd('CtrlPLinePre', '^>')
  retu s:results
endf

fu! s:CtrlPMatchPy(items, str, limit, mmode, ispath, crfile, regex) abort
  if !has('python3') && !has('python')
    retu ['ERROR: cpsm requires Vim built with Python or Python3 support']
  elsei s:status == 0
    retu ['ERROR: failed to load cpsm module']
  elsei s:status == 1
    retu ['ERROR: cpsm built with version of Python not supported by Vim']
  en

  if empty(a:str) && g:cpsm_match_empty_query == 0
    let s:results = a:items[0:(a:limit)]
    let s:regexes = []
  el
    let s:match_crfile = exists('g:ctrlp_match_current_file') ? g:ctrlp_match_current_file : 0
    let s:regex_line_prefix = '> '
    if exists('g:ctrlp_line_prefix')
      let s:regex_line_prefix = g:ctrlp_line_prefix
    en
    let s:input = {
    \   'items': a:items,
    \   'query': a:str,
    \   'limit': a:limit,
    \   'mmode': a:mmode,
    \   'ispath': a:ispath,
    \   'crfile': a:crfile,
    \   'highlight_mode': g:cpsm_highlight_mode,
    \   'match_crfile': s:match_crfile,
    \   'max_threads': g:cpsm_max_threads,
    \   'query_inverting_delimiter': g:cpsm_query_inverting_delimiter,
    \   'regex_line_prefix': s:regex_line_prefix,
    \   'unicode': g:cpsm_unicode,
    \ }
    if s:status == 3
      let s:output = py3eval('_ctrlp_match_evalinput()')
    el
      let s:output = pyeval('_ctrlp_match_evalinput()')
    en
    let s:results = s:output[0]
    let s:regexes = s:output[1]
  en

  cal clearmatches()
  " Apply highlight regexes.
  for r in s:regexes
    cal matchadd('CtrlPMatch', r)
  endfo
  " CtrlP does this match to hide the leading > in results.
  cal matchadd('CtrlPLinePre', '^>')
  retu s:results
endf

let s:status = 0
if has('cpsm') || has('cpsm/dyn')
  let s:func = function('s:CtrlPMatchC')
  if has('cpsm/dyn') && (&cpsmdll == '')
    let &cpsmdll = s:script_dir .. '/libcpsm_vim.so'
  en
  try
    cal cpsm_ctrlp_match([''], '')
    let s:status = 2
  cat
    let s:status = 1
  endt
el
  let s:func = function('s:CtrlPMatchPy')
  " s:status is:
  " - 0: no Python support, or module loading failed for other reasons
  " - 1: cpsm module built with incompatible version of Python
  " - 2: cpsm module usable with Python 2
  " - 3: cpsm module usable with Python 3
  if has('python3')
    try
      exe 'py3file ' . s:script_dir . '/cpsm.py'
      let s:status = 3
    cat
      " Ideally we'd check specifically for the exception
      " 'ImportError: dynamic module does not define module export function',
      " but Vim's handling of multiline exceptions seems to be completely
      " broken.
      if !has('python')
        let s:status = 1
      en
    endt
  en
  if s:status == 0 && has('python')
    try
      exe 'pyfile ' . s:script_dir . '/cpsm.py'
      let s:status = 2
    cat
      let s:status = 1
    endt
  en
en

fu cpsm#CtrlPMatch(items, str, limit, mmode, ispath, crfile, regex)
  retu s:func(a:items, a:str, a:limit, a:mmode, a:ispath, a:crfile, a:regex)
endf
