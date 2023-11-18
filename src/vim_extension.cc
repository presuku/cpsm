// cpsm - fuzzy path matcher
// Copyright (C) 2015 the Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <cstring>
#include <utility>

#include "api.h"
#include "ctrlp_util.h"
#include "par_util.h"
#include "str_util.h"

extern "C" {

#include "vim.h"

long (*vim_list_len)(list_T *l) = NULL;
listitem_T *(*vim_list_find)(list_T *l, long n) = NULL;
int (*vim_list_append_string)(list_T *l, char_u *str, int len) = NULL;
}

namespace {

inline bool VimString_AsStringAndSize(listitem_T* li, char** data,
                                        std::size_t* size) {
	if (li->li_tv.v_type == VAR_STRING) {
	    *data = (char *)li->li_tv.vval.v_string;
        *size = std::strlen(*data);
        return true;
    } else {
        return false;
    }
}

// Item type that wraps another object.
template <typename InnerItem>
struct VimObjItem {
  InnerItem inner;

  VimObjItem() {}
  explicit VimObjItem(InnerItem inner)
      : inner(std::move(inner)) {}

  string_view match_key() const { return inner.match_key(); }
  string_view sort_key() const { return inner.sort_key(); }
};

// Lists hold references on their elements, so we can use borrowed references.
template <typename MatchMode>
using VimListCtrlPItem =
    VimObjItem<cpsm::CtrlPItem<cpsm::StringRefItem, MatchMode>>;

// Thread-safe item source that batches items from a Vimthon list.
template <typename MatchMode>
class VimListCtrlPMatchSource {
 public:
  using Item = VimListCtrlPItem<MatchMode>;

  explicit VimListCtrlPMatchSource(list_T* const list) : list_(list) {
    size_ = vim_list_len(list);
    if (size_ < 0) {
      throw cpsm::Error("input is not a list");
    }
  }

  bool fill(std::vector<Item>& items) {
    std::lock_guard<std::mutex> lock(mu_);
    if (done_) {
      return false;
    }
    auto const add_item = [&](listitem_T* item_obj) {
      if (item_obj == nullptr) {
        return false;
      }
      char* item_data;
      std::size_t item_size;
      if (!VimString_AsStringAndSize(item_obj, &item_data, &item_size)) {
        return false;
      }
      items.emplace_back(
          cpsm::CtrlPItem<cpsm::StringRefItem, MatchMode>(
              (cpsm::StringRefItem(string_view(item_data, item_size)))));
      return true;
    };
    std::size_t const max = std::min(i_ + batch_size(), size_);
    for (; i_ < max; i_++) {
      if (!add_item(vim_list_find(list_, i_))) {
        done_ = true;
        return false;
      }
    }
    return i_ != size_;
  }

  static constexpr long long batch_size() { return 512; }

 private:
  std::mutex mu_;
  list_T* const list_;
  std::size_t i_ = 0;
  std::size_t size_ = 0;
  bool done_ = false;
};

// `dst` must be a functor compatible with signature `void(string_view
// item, string_view match_key, cpsm::MatchInfo* info)`.
template <typename Sink>
void for_each_vimctrlp_match(string_view const query,
                            cpsm::Options const& opts,
                            cpsm::CtrlPMatchMode const match_mode,
                            list_T* const items_iter, Sink&& dst) {
#define DO_MATCH_WITH(MMODE)                                                   \
    cpsm::for_each_match<VimListCtrlPItem<MMODE>>(                              \
        query, opts, VimListCtrlPMatchSource<MMODE>(items_iter),                \
        [&](VimListCtrlPItem<MMODE> const& item, cpsm::MatchInfo* const info) { \
          dst(item.inner.inner.item(), item.match_key(), info);      \
        });
  switch (match_mode) {
    case cpsm::CtrlPMatchMode::FULL_LINE:
      DO_MATCH_WITH(cpsm::FullLineMatch);
      break;
    case cpsm::CtrlPMatchMode::FILENAME_ONLY:
      DO_MATCH_WITH(cpsm::FilenameOnlyMatch);
      break;
    case cpsm::CtrlPMatchMode::FIRST_NON_TAB:
      DO_MATCH_WITH(cpsm::FirstNonTabMatch);
      break;
    case cpsm::CtrlPMatchMode::UNTIL_LAST_TAB:
      DO_MATCH_WITH(cpsm::UntilLastTabMatch);
      break;
  }
#undef DO_MATCH_WITH
};

unsigned int get_nr_threads(unsigned int const max_threads) {
  std::size_t nr_threads = cpsm::Thread::hardware_concurrency();
  if (!nr_threads) {
    nr_threads = 1;
  }
  if (max_threads && (nr_threads > max_threads)) {
    nr_threads = max_threads;
  }
  return nr_threads;
}

}  // namespace


int VimArg_ParseKeywords(cpsm_T *args,
                         list_T **items_obj,
                         char const **query_data, std::size_t *query_size,
                         int *limit_int,
                         char const **mmode_data, std::size_t *mmode_size,
                         int *is_path,
                         char const **crfile_data, std::size_t *crfile_size,
                         char const **highlight_mode_data, std::size_t *highlight_mode_size,
                         int *match_crfile,
                         int *max_threads_int,
                         char const **query_inverting_delimiter_data, std::size_t *query_inverting_delimiter_size,
                         char const **regex_line_prefix_data, std::size_t *regex_line_prefix_size,
                         int *unicode
                         )
{
    *items_obj = args->items_obj;
    *query_data = args->query_data;
    *query_size = args->query_size;
    *limit_int  = args->limit_int;
    *mmode_data = args->mmode_data;
    *mmode_size = args->mmode_size;
    *is_path = args->is_path;
    *crfile_data = args->crfile_data;
    *crfile_size = args->crfile_size;
    *highlight_mode_data = args->highlight_mode_data;
    *highlight_mode_size = args->highlight_mode_size;
    *match_crfile = args->match_crfile;
    *max_threads_int = args->max_threads_int;
    *query_inverting_delimiter_data = args->query_inverting_delimiter_data;
    *query_inverting_delimiter_size = args->query_inverting_delimiter_size;
    *regex_line_prefix_data = args->regex_line_prefix_data;
    *regex_line_prefix_size = args->regex_line_prefix_size;
    *unicode = args->unicode;
    vim_list_len = args->list_len;
    vim_list_find = args->list_find;
    vim_list_append_string = args->list_append_string;
    return 1;
}


extern "C" {

DLLEXPORT void cpsm_ctrlp_match(cpsm_T *args, typval_T *rettv)
{
  // Required parameters.
  list_T* items_obj;
  char const* query_data;
  std::size_t query_size;
  // CtrlP-provided options.
  int limit_int = -1;
  char const* mmode_data = nullptr;
  std::size_t mmode_size = 0;
  int is_path = 0;
  char const* crfile_data = nullptr;
  std::size_t crfile_size = 0;
  // cpsm-specific options.
  char const* highlight_mode_data = nullptr;
  std::size_t highlight_mode_size = 0;
  int match_crfile = 0;
  int max_threads_int = 0;
  char const* query_inverting_delimiter_data = nullptr;
  std::size_t query_inverting_delimiter_size = 0;
  char const* regex_line_prefix_data = nullptr;
  std::size_t regex_line_prefix_size = 0;
  int unicode = 0;

  if (!VimArg_ParseKeywords(
          args,
          &items_obj, &query_data, &query_size, &limit_int, &mmode_data,
          &mmode_size, &is_path, &crfile_data, &crfile_size,
          &highlight_mode_data, &highlight_mode_size, &match_crfile,
          &max_threads_int, &query_inverting_delimiter_data,
          &query_inverting_delimiter_size, &regex_line_prefix_data,
          &regex_line_prefix_size, &unicode)) {
    return;
  }

  try {
    std::string query(query_data, query_size);
    string_view query_inverting_delimiter(query_inverting_delimiter_data,
                                                query_inverting_delimiter_size);

    if (!query_inverting_delimiter.empty()) {
      if (query_inverting_delimiter.size() > 1) {
        throw cpsm::Error(
            "query inverting delimiter must be a single character");
      }
      auto split_words = cpsm::str_split(query, query_inverting_delimiter[0]);
      std::reverse(std::begin(split_words), std::end(split_words));
      query = cpsm::str_join(split_words, "");
    }

    auto const mopts =
        cpsm::Options()
            .set_crfile(string_view(crfile_data, crfile_size))
            .set_limit((limit_int >= 0) ? std::size_t(limit_int) : 0)
            .set_match_crfile(match_crfile)
            .set_nr_threads(
                 get_nr_threads((max_threads_int >= 0)
                                    ? static_cast<unsigned int>(max_threads_int)
                                    : 0))
            .set_path(is_path)
            .set_unicode(unicode)
            .set_want_match_info(true);
    string_view const highlight_mode(highlight_mode_data,
                                           highlight_mode_size);

    listitem_T *li;
    li = vim_list_find(rettv->vval.v_list, 0);
    if (li == NULL || li->li_tv.vval.v_list == NULL)
        return;
    list_T * matches_list = li->li_tv.vval.v_list;
    if (!matches_list) {
        return;
    }

    std::vector<std::string> highlight_regexes;
    for_each_vimctrlp_match(
        query, mopts,
        cpsm::parse_ctrlp_match_mode(string_view(mmode_data, mmode_size)),
        items_obj,
        [&](string_view const item, string_view const match_key,
            cpsm::MatchInfo* const info) {
          if (vim_list_append_string(matches_list,
              (char_u*)match_key.data(), match_key.size()) == FAIL) {
            throw cpsm::Error("match appending failed");
          }
          auto match_positions = info->match_positions();
          // Adjust match positions to account for substringing.
          std::size_t const delta = match_key.data() - item.data();
          for (auto& pos : match_positions) {
            pos += delta;
          }
          cpsm::get_highlight_regexes(
              highlight_mode, item, match_positions, highlight_regexes,
              string_view(regex_line_prefix_data,
                                regex_line_prefix_size));
          });
    li = vim_list_find(rettv->vval.v_list, 1);
    if (li == NULL || li->li_tv.vval.v_list == NULL)
        return;
    list_T * regexes_list = li->li_tv.vval.v_list;
    if (!regexes_list)
        return;
    for (auto const& regex : highlight_regexes) {
        if (vim_list_append_string(regexes_list,
                               (char_u *)regex.data(),
                               regex.size()) == FAIL) {
            throw cpsm::Error("regex appending failed");
        }
    }
        return;
  } catch (std::exception const& ex) {
      /* std::cout <<  "exception:" << ex.what() << std::endl; */
      return;
  }
  return;
}

} /* extern "C" */
