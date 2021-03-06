/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>
          (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

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

#ifndef CURRENT_TYPE_SYSTEM_BASE_H
#define CURRENT_TYPE_SYSTEM_BASE_H

namespace current {

namespace reflection {

// Instantiation types.
struct DeclareFields {};
struct CountFields {};

}  // namespace reflection

namespace reflection {

// Dummy type for `CountFields` instantiation type.
struct CountFieldsImplementationType {
  template <typename... T>
  CountFieldsImplementationType(T&&...) {}
  long long x[100];  // TODO(dkorolev): Fix JSON parse/serialize on Windows. Gotta go deeper. -- D.K.
};

// Helper structs for reflection.
class FieldTypeAndName {};
class FieldTypeAndNameAndIndex {};

template <typename SELF>
class FieldNameAndPtr {};

class FieldNameAndImmutableValue {};
class FieldNameAndMutableValue {};

// Simple index. TODO(dkorolev): Retire the complex `Index` below, it may speed up compilation.
template <int N>
struct SimpleIndex {};

// Complex index: <HelperStruct, int Index>.
template <class T, int N>
struct Index {};

// `CURRENT_REFLECTION()` for `Index<FieldType, n>` calls `f(TypeSelector<real_field_type>)`.
template <typename T>
struct TypeSelector {};

}  // namespace reflection
}  // namespace current

#endif  // CURRENT_TYPE_SYSTEM_BASE_H
