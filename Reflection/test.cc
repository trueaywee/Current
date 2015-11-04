#include "reflection.h"

#include "../3rdparty/gtest/gtest-main.h"

namespace valid_struct_test {
CURRENT_STRUCT(Empty){};
CURRENT_STRUCT(EmptyDerived, Empty){};
struct NotCurrentStruct {
  int x;
};
struct SimplyDerivedFromCurrentSuper : ::current::reflection::CurrentSuper {};
struct NotCurrentStructDerivedFromCurrentStruct : Empty {};
CURRENT_STRUCT(MessedWithCOUNTER) {
  CURRENT_FIELD(i1, uint64_t);
  static size_t GetCounter() { return __COUNTER__; }
  CURRENT_FIELD(i2, uint64_t);
};

static_assert(IS_VALID_CURRENT_STRUCT(Empty), "`Empty` must pass `IS_VALID_CURRENT_STRUCT` check.");
static_assert(IS_VALID_CURRENT_STRUCT(EmptyDerived),
              "`EmptyDerived` must pass `IS_VALID_CURRENT_STRUCT` check.");

// The lines below don't compile with various errors.
// static_assert(!IS_VALID_CURRENT_STRUCT(NotCurrentStruct),
//               "`NotCurrentStruct` must NOT pass `IS_VALID_CURRENT_STRUCT` check.");
// static_assert(!IS_VALID_CURRENT_STRUCT(SimplyDerivedFromCurrentSuper),
//               "`SimplyDerivedFromCurrentSuper` must NOT pass `IS_VALID_CURRENT_STRUCT` check.");
// static_assert(!IS_VALID_CURRENT_STRUCT(NotCurrentStructDerivedFromCurrentStruct),
//               "`NotCurrentStructDerivedFromCurrentStruct` must NOT pass `IS_VALID_CURRENT_STRUCT` check.");
// static_assert(!IS_VALID_CURRENT_STRUCT(MessedWithCOUNTER),
//               "`MessedWithCOUNTER` must not pass `IS_VALID_CURRENT_STRUCT` check.");
}

namespace reflection_test {
CURRENT_STRUCT(Foo) { CURRENT_FIELD(i, uint64_t, 42u); };
CURRENT_STRUCT(Bar) {
  CURRENT_FIELD(v1, std::vector<uint64_t>);
  CURRENT_FIELD(v2, std::vector<Foo>);
  CURRENT_FIELD(v3, std::vector<std::vector<Foo>>);
};
CURRENT_STRUCT(DerivedFromFoo, Foo) { CURRENT_FIELD(bar, Bar); };

static_assert(IS_VALID_CURRENT_STRUCT(Foo), "Struct `Foo` was not properly declared.");
static_assert(IS_VALID_CURRENT_STRUCT(Bar), "Struct `Bar` was not properly declared.");
static_assert(IS_VALID_CURRENT_STRUCT(DerivedFromFoo), "Struct `DerivedFromFoo` was not properly declared.");
}  // namespace reflection_test

namespace some_other_namespace {
static_assert(IS_VALID_CURRENT_STRUCT(::reflection_test::Foo), "Struct `Foo` was not properly declared.");
static_assert(IS_VALID_CURRENT_STRUCT(::reflection_test::Bar), "Struct `Bar` was not properly declared.");
static_assert(IS_VALID_CURRENT_STRUCT(::reflection_test::DerivedFromFoo),
              "Struct `DerivedFromFoo` was not properly declared.");
}  // namespace some_other_namespace

static_assert(IS_VALID_CURRENT_STRUCT(reflection_test::Foo), "Struct `Foo` was not properly declared.");
static_assert(IS_VALID_CURRENT_STRUCT(reflection_test::Bar), "Struct `Bar` was not properly declared.");
static_assert(IS_VALID_CURRENT_STRUCT(reflection_test::DerivedFromFoo),
              "Struct `DerivedFromFoo` was not properly declared.");

using current::reflection::Reflector;

TEST(Reflection, CurrentStructInternals) {
  using namespace reflection_test;
  using namespace current::reflection;

  static_assert(std::is_same<SuperType<Foo>, CurrentSuper>::value, "");
  EXPECT_EQ(1u, FieldCounter<Foo>::value);

  std::string field_name;
  Foo::CURRENT_REFLECTION([&field_name](const std::string& name) { field_name = name; }, Index<FieldName, 0>());
  EXPECT_EQ("i", field_name);

  bool field_type_correct = false;
  Foo::CURRENT_REFLECTION([&field_type_correct](TypeSelector<uint64_t>) { field_type_correct = true; },
                          Index<FieldType, 0>());
  EXPECT_TRUE(field_type_correct);

  Foo foo;
  uint64_t field_value = 0u;
  foo.CURRENT_REFLECTION([&field_value](uint64_t value) { field_value = value; }, Index<FieldValue, 0>());
  EXPECT_EQ(42u, field_value);

  foo.i = 100u;
  foo.CURRENT_REFLECTION([&field_name, &field_value](const std::string& name, const uint64_t& value) {
    field_name = name;
    field_value = value;
  }, Index<FieldNameAndImmutableValueReference, 0>());
  EXPECT_EQ("i", field_name);
  EXPECT_EQ(100u, field_value);

  foo.CURRENT_REFLECTION([&field_name](const std::string& name, uint64_t& value) {
    field_name = name;
    value = 123u;
  }, Index<FieldNameAndMutableValueReference, 0>());
  EXPECT_EQ("i", field_name);
  EXPECT_EQ(123u, foo.i);

  static_assert(std::is_same<SuperType<Bar>, CurrentSuper>::value, "");
  EXPECT_EQ(3u, FieldCounter<Bar>::value);
  static_assert(std::is_same<SuperType<DerivedFromFoo>, Foo>::value, "");
  EXPECT_EQ(1u, FieldCounter<DerivedFromFoo>::value);
}

TEST(Reflection, DescribeCppStruct) {
  using namespace reflection_test;

  EXPECT_EQ(
      "struct Foo {\n"
      "  uint64_t i;\n"
      "};\n",
      Reflector().DescribeCppStruct<Foo>());

  EXPECT_EQ(
      "struct Bar {\n"
      "  std::vector<uint64_t> v1;\n"
      "  std::vector<Foo> v2;\n"
      "  std::vector<std::vector<Foo>> v3;\n"
      "};\n",
      Reflector().DescribeCppStruct<Bar>());

  EXPECT_EQ(
      "struct DerivedFromFoo : Foo {\n"
      "  Bar bar;\n"
      "};\n",
      Reflector().DescribeCppStruct<DerivedFromFoo>());

  EXPECT_EQ(7u, Reflector().KnownTypesCountForUnitTest());
}

TEST(Reflection, TypeID) {
  using namespace reflection_test;
  using current::reflection::ReflectedType_Struct;
  using current::reflection::TYPEID_COLLECTION_TYPE;
  using current::reflection::TYPEID_TYPE_RANGE;

  // TODO(dkorolev): Migrate to `Polymorphic<>` and avoid `dynamic_cast<>` here.
  ReflectedType_Struct* bar = dynamic_cast<ReflectedType_Struct*>(Reflector().ReflectType<Bar>());
  EXPECT_LE(TYPEID_COLLECTION_TYPE, static_cast<uint64_t>(bar->fields[0].first->type_id));
  EXPECT_GT(TYPEID_COLLECTION_TYPE + TYPEID_TYPE_RANGE, static_cast<uint64_t>(bar->fields[0].first->type_id));
  EXPECT_LE(TYPEID_COLLECTION_TYPE, static_cast<uint64_t>(bar->fields[1].first->type_id));
  EXPECT_GT(TYPEID_COLLECTION_TYPE + TYPEID_TYPE_RANGE, static_cast<uint64_t>(bar->fields[1].first->type_id));
  EXPECT_LE(TYPEID_COLLECTION_TYPE, static_cast<uint64_t>(bar->fields[2].first->type_id));
  EXPECT_GT(TYPEID_COLLECTION_TYPE + TYPEID_TYPE_RANGE, static_cast<uint64_t>(bar->fields[2].first->type_id));
  EXPECT_NE(bar->fields[0].first->type_id, bar->fields[1].first->type_id);
  EXPECT_NE(bar->fields[1].first->type_id, bar->fields[2].first->type_id);
}