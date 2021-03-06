/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>

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

#ifndef CURRENT_TYPE_SYSTEM_SERIALIZATION_BINARY_H
#define CURRENT_TYPE_SYSTEM_SERIALIZATION_BINARY_H

#include <chrono>  // For the future use of "primitive_types.dsl.h".

#include "base.h"

// Basic types.
#include "types/primitive.h"
#include "types/enum.h"

// STL containers.
#include "types/vector.h"
#include "types/pair.h"
#include "types/map.h"

// Current types.
#include "types/current_typeid.h"
#include "types/current_struct.h"
#include "types/optional.h"
#include "types/variant.h"

namespace current {
namespace serialization {
namespace binary {

template <typename T>
inline void SaveIntoBinary(std::ostream& ostream, const T& source) {
  using DECAYED_T = current::decay<T>;
  save::SaveIntoBinaryImpl<DECAYED_T>::Save(ostream, source);
}

template <typename T>
inline T LoadFromBinary(std::istream& istream) {
  T result;
  load::LoadFromBinaryImpl<T>::Load(istream, result);
  return result;
}

}  // namespace binary
}  // namespace serialization
}  // namespace current

using current::serialization::binary::SaveIntoBinary;
using current::serialization::binary::LoadFromBinary;

#endif  // CURRENT_TYPE_SYSTEM_SERIALIZATION_BINARY_H
