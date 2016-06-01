/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2016 Grigory Nikolaenko <nikolaenko.grigory@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#ifndef CURRENT_STORAGE_CONTAINER_ONE_TO_MANY_H
#define CURRENT_STORAGE_CONTAINER_ONE_TO_MANY_H

#include "common.h"
#include "sfinae.h"

#include "../base.h"

#include "../../TypeSystem/optional.h"
#include "../../Bricks/util/comparators.h"  // For `CurrentHashFunction`.
#include "../../Bricks/util/iterator.h"     // For `GenericMapIterator` and `GenericMapAccessor`.
#include "../../Bricks/util/singleton.h"

namespace current {
namespace storage {
namespace container {

template <typename T,
          typename UPDATE_EVENT,
          typename DELETE_EVENT,
          template <typename...> class ROW_MAP,
          template <typename...> class COL_MAP>
class GenericOneToMany {
 public:
  using row_t = sfinae::entry_row_t<T>;
  using col_t = sfinae::entry_col_t<T>;
  using key_t = std::pair<row_t, col_t>;
  using elements_map_t = std::unordered_map<key_t, std::unique_ptr<T>, CurrentHashFunction<key_t>>;
  using row_elements_map_t = COL_MAP<col_t, const T*>;
  using forward_map_t = ROW_MAP<row_t, row_elements_map_t>;
  using transposed_map_t = row_elements_map_t;
  using rest_behavior_t = rest::behavior::Matrix;

  GenericOneToMany(MutationJournal& journal) : journal_(journal) {}

  bool Empty() const { return map_.empty(); }
  size_t Size() const { return map_.size(); }

  // Adds specified object and overwrites existing one if it has the same row and col.
  // Removes all other existing objects with the same col.
  void Add(const T& object) {
    const auto row = sfinae::GetRow(object);
    const auto col = sfinae::GetCol(object);
    const auto key = std::make_pair(row, col);
    const auto cit = map_.find(key);
    if (cit != map_.end()) {
      const T& previous_object = *(cit->second);
      journal_.LogMutation(UPDATE_EVENT(object),
                           [this, key, previous_object]() { DoAdd(key, previous_object); });
    } else {
      const auto cit = transposed_.find(col);
      if (cit != transposed_.end()) {
        const T& previous_object = *(cit->second);
        const auto previous_key = std::make_pair(sfinae::GetRow(previous_object), col);
        journal_.LogMutation(DELETE_EVENT(previous_object),
                             [this, previous_key, previous_object]() { DoAdd(previous_key, previous_object); });
        DoErase(previous_key);
      }
      journal_.LogMutation(UPDATE_EVENT(object), [this, key]() { DoErase(key); });
    }
    DoAdd(key, object);
  }

  void Erase(const key_t& key) {
    const auto cit = map_.find(key);
    if (cit != map_.end()) {
      const T& previous_object = *(cit->second);
      journal_.LogMutation(DELETE_EVENT(previous_object),
                           [this, key, previous_object]() { DoAdd(key, previous_object); });
      DoErase(key);
    }
  }
  void Erase(sfinae::CF<row_t> row, sfinae::CF<col_t> col) { Erase(std::make_pair(row, col)); }

  void EraseCol(sfinae::CF<col_t> col) {
    const auto cit = transposed_.find(col);
    if (cit != transposed_.end()) {
      const T& previous_object = *(cit->second);
      const auto key = std::make_pair(sfinae::GetRow(previous_object), col);
      journal_.LogMutation(DELETE_EVENT(previous_object),
                           [this, key, previous_object]() { DoAdd(key, previous_object); });
      DoErase(key);
    }
  }

  ImmutableOptional<T> operator[](const key_t& key) const {
    const auto cit = map_.find(key);
    if (cit != map_.end()) {
      return ImmutableOptional<T>(FromBarePointer(), cit->second.get());
    } else {
      return nullptr;
    }
  }
  ImmutableOptional<T> Get(sfinae::CF<row_t> row, sfinae::CF<col_t> col) const {
    return operator[](std::make_pair(row, col));
  }
  ImmutableOptional<T> GetEntryFromCol(sfinae::CF<col_t> col) const {
    const auto cit = transposed_.find(col);
    if (cit != transposed_.end()) {
      return ImmutableOptional<T>(FromBarePointer(), cit->second);
    } else {
      return nullptr;
    }
  }

  bool DoesNotConflict(const key_t& key) const { return transposed_.find(key.second) == transposed_.end(); }
  bool DoesNotConflict(sfinae::CF<row_t> row, sfinae::CF<col_t> col) const {
    return DoesNotConflict(std::make_pair(row, col));
  }

  void operator()(const UPDATE_EVENT& e) {
    const auto row = sfinae::GetRow(e.data);
    const auto col = sfinae::GetCol(e.data);
    DoAdd(std::make_pair(row, col), e.data);
  }
  void operator()(const DELETE_EVENT& e) { DoErase(std::make_pair(e.key.first, e.key.second)); }

  template <typename ROWS_MAP>
  struct RowsAccessor final {
    using key_t = typename ROWS_MAP::key_type;
    using elements_map_t = typename ROWS_MAP::mapped_type;
    const ROWS_MAP& map_;

    struct RowsIterator final {
      using iterator_t = typename ROWS_MAP::const_iterator;
      iterator_t iterator;
      explicit RowsIterator(iterator_t iterator) : iterator(iterator) {}
      void operator++() { ++iterator; }
      bool operator==(const RowsIterator& rhs) const { return iterator == rhs.iterator; }
      bool operator!=(const RowsIterator& rhs) const { return !operator==(rhs); }
      sfinae::CF<key_t> key() const { return iterator->first; }
      GenericMapAccessor<elements_map_t> operator*() const {
        return GenericMapAccessor<elements_map_t>(iterator->second);
      }
    };

    explicit RowsAccessor(const ROWS_MAP& map) : map_(map) {}

    bool Empty() const { return map_.empty(); }
    size_t Size() const { return map_.size(); }

    bool Has(const key_t& x) const { return map_.find(x) != map_.end(); }

    ImmutableOptional<GenericMapAccessor<elements_map_t>> operator[](key_t key) const {
      const auto iterator = map_.find(key);
      if (iterator != map_.end()) {
        return std::move(std::make_unique<GenericMapAccessor<elements_map_t>>(iterator->second));
      } else {
        return nullptr;
      }
    }

    RowsIterator begin() const { return RowsIterator(map_.cbegin()); }
    RowsIterator end() const { return RowsIterator(map_.cend()); }
  };

  RowsAccessor<forward_map_t> Rows() const { return RowsAccessor<forward_map_t>(forward_); }

  GenericMapAccessor<transposed_map_t> Cols() const {
    return GenericMapAccessor<transposed_map_t>(transposed_);
  }

  GenericMapAccessor<row_elements_map_t> Row(sfinae::CF<row_t> row) const {
    const auto cit = forward_.find(row);
    return GenericMapAccessor<row_elements_map_t>(
        cit != forward_.end() ? cit->second : current::ThreadLocalSingleton<row_elements_map_t>());
  }

  // For REST, iterate over all the elements of the OneToMany, in no particular order.
  using iterator_t = GenericMapIterator<elements_map_t>;
  iterator_t begin() const { return iterator_t(map_.begin()); }
  iterator_t end() const { return iterator_t(map_.end()); }

 private:
  void DoErase(const key_t& key) {
    auto& map_row = forward_[key.first];
    map_row.erase(key.second);
    if (map_row.empty()) {
      forward_.erase(key.first);
    }
    transposed_.erase(key.second);
    map_.erase(key);
  }

  void DoAdd(const key_t& key, const T& object) {
    auto& placeholder = map_[key];
    placeholder = std::make_unique<T>(object);
    forward_[key.first][key.second] = placeholder.get();
    transposed_[key.second] = placeholder.get();
  }

  elements_map_t map_;
  forward_map_t forward_;
  transposed_map_t transposed_;
  MutationJournal& journal_;
};

template <typename T, typename UPDATE_EVENT, typename DELETE_EVENT>
using UnorderedOneToUnorderedMany = GenericOneToMany<T, UPDATE_EVENT, DELETE_EVENT, Unordered, Unordered>;

template <typename T, typename UPDATE_EVENT, typename DELETE_EVENT>
using OrderedOneToOrderedMany = GenericOneToMany<T, UPDATE_EVENT, DELETE_EVENT, Ordered, Ordered>;

template <typename T, typename UPDATE_EVENT, typename DELETE_EVENT>
using UnorderedOneToOrderedMany = GenericOneToMany<T, UPDATE_EVENT, DELETE_EVENT, Unordered, Ordered>;

template <typename T, typename UPDATE_EVENT, typename DELETE_EVENT>
using OrderedOneToUnorderedMany = GenericOneToMany<T, UPDATE_EVENT, DELETE_EVENT, Ordered, Unordered>;

}  // namespace container

template <typename T, typename E1, typename E2>  // Entry, update event, delete event.
struct StorageFieldTypeSelector<container::UnorderedOneToUnorderedMany<T, E1, E2>> {
  static const char* HumanReadableName() { return "UnorderedOneToUnorderedMany"; }
};

template <typename T, typename E1, typename E2>  // Entry, update event, delete event.
struct StorageFieldTypeSelector<container::OrderedOneToOrderedMany<T, E1, E2>> {
  static const char* HumanReadableName() { return "OrderedOneToOrderedMany"; }
};

template <typename T, typename E1, typename E2>  // Entry, update event, delete event.
struct StorageFieldTypeSelector<container::UnorderedOneToOrderedMany<T, E1, E2>> {
  static const char* HumanReadableName() { return "UnorderedOneToOrderedMany"; }
};

template <typename T, typename E1, typename E2>  // Entry, update event, delete event.
struct StorageFieldTypeSelector<container::OrderedOneToUnorderedMany<T, E1, E2>> {
  static const char* HumanReadableName() { return "OrderedOneToUnorderedMany"; }
};

}  // namespace storage
}  // namespace current

using current::storage::container::UnorderedOneToUnorderedMany;
using current::storage::container::OrderedOneToOrderedMany;
using current::storage::container::UnorderedOneToOrderedMany;
using current::storage::container::OrderedOneToUnorderedMany;

#endif  // CURRENT_STORAGE_CONTAINER_ONE_TO_MANY_H
