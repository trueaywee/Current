/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

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

// TODO(dkorolev): Death tests, as well as human-readable error messages phrasing.

#include "infer.h"

#include "../../Bricks/file/file.h"
#include "../../Bricks/dflags/dflags.h"
#include "../../3rdparty/gtest/gtest-main-with-dflags.h"

DEFINE_bool(regenerate_golden_inferred_schemas,
            false,
            "Set to 'true' to re-generate golden inferred schema files.");

static std::vector<std::string> ListGoldenFilesWithExtension(const std::string& dir, const std::string& ext) {
  std::vector<std::string> names;
  const std::string suffix = '.' + ext;
  current::FileSystem::ScanDir(dir,
                               [&](const std::string& filename) {
                                 if (filename.length() >= suffix.length()) {
                                   const std::string prefix =
                                       filename.substr(0, filename.length() - suffix.length());
                                   if (prefix + suffix == filename) {
                                     names.push_back(prefix);
                                   }
                                 }
                               });
  return names;
}

TEST(InferJSONSchema, MatchAgainstGoldenFiles) {
  const std::string golden_dir = "golden";
  const std::vector<std::string> cases = ListGoldenFilesWithExtension(golden_dir, "json_data");
  for (const auto& test : cases) {
    const std::string filename_prefix = current::FileSystem::JoinPath("golden", test);
    const std::string json = current::FileSystem::ReadFileAsString(filename_prefix + ".json_data");
    if (!FLAGS_regenerate_golden_inferred_schemas) {
      EXPECT_EQ(current::FileSystem::ReadFileAsString(filename_prefix + ".raw"),
                JSON<JSONFormat::Minimalistic>(current::utils::InferRawSchemaFromJSON(json)))
          << "While running test case `" << test << "`.";
      EXPECT_EQ(current::FileSystem::ReadFileAsString(filename_prefix + ".tsv"),
                current::utils::JSONSchemaAsTSV(json))
          << "While running test case `" << test << "`.";
      EXPECT_EQ(current::FileSystem::ReadFileAsString(filename_prefix + ".schema"),
                current::utils::JSONSchemaAsCurrentStructs(json))
          << "While running test case `" << test << "`.";
    } else {
      current::FileSystem::WriteStringToFile(
          JSON<JSONFormat::Minimalistic>(current::utils::InferRawSchemaFromJSON(json)),
          (filename_prefix + ".raw").c_str());
      current::FileSystem::WriteStringToFile(current::utils::JSONSchemaAsTSV(json),
                                             (filename_prefix + ".tsv").c_str());
      current::FileSystem::WriteStringToFile(current::utils::JSONSchemaAsCurrentStructs(json),
                                             (filename_prefix + ".schema").c_str());
    }
  }
}

// RapidJSON usage snippets framed as unit tests. Let's keep them in this `test.cc`. -- D.K.
TEST(RapidJSON, Smoke) {
  using rapidjson::Document;
  using rapidjson::Value;
  using rapidjson::Writer;
  using rapidjson::OStreamWrapper;

  std::string json;

  {
    Document document;
    auto& allocator = document.GetAllocator();
    Value foo("bar");
    document.SetObject().AddMember("foo", foo, allocator);

    EXPECT_TRUE(document.IsObject());
    EXPECT_FALSE(document.IsArray());
    EXPECT_TRUE(document.HasMember("foo"));
    EXPECT_TRUE(document["foo"].IsString());
    EXPECT_EQ("bar", document["foo"].GetString());

    std::ostringstream os;
    OStreamWrapper stream(os);
    Writer<OStreamWrapper> writer(stream);
    document.Accept(writer);
    json = os.str();
  }

  EXPECT_EQ("{\"foo\":\"bar\"}", json);

  {
    Document document;
    ASSERT_FALSE(document.Parse<0>(json.c_str()).HasParseError());
    EXPECT_TRUE(document.IsObject());
    EXPECT_TRUE(document.HasMember("foo"));
    EXPECT_TRUE(document["foo"].IsString());
    EXPECT_EQ(std::string("bar"), document["foo"].GetString());
    EXPECT_FALSE(document.HasMember("bar"));
    EXPECT_FALSE(document.HasMember("meh"));
  }
}

TEST(RapidJSON, Array) {
  using rapidjson::Document;
  using rapidjson::Value;
  using rapidjson::Writer;
  using rapidjson::OStreamWrapper;

  std::string json;

  {
    Document document;
    auto& allocator = document.GetAllocator();
    document.SetArray();
    Value element;
    element = 42;
    document.PushBack(element, allocator);
    element = "bar";
    document.PushBack(element, allocator);

    EXPECT_TRUE(document.IsArray());
    EXPECT_FALSE(document.IsObject());

    std::ostringstream os;
    OStreamWrapper stream(os);
    Writer<OStreamWrapper> writer(stream);
    document.Accept(writer);
    json = os.str();
  }

  EXPECT_EQ("[42,\"bar\"]", json);
}

TEST(RapidJSON, NullInString) {
  using rapidjson::Document;
  using rapidjson::Value;
  using rapidjson::Writer;
  using rapidjson::OStreamWrapper;

  std::string json;

  {
    Document document;
    auto& allocator = document.GetAllocator();
    Value s;
    s.SetString("terrible\0avoided", strlen("terrible") + 1 + strlen("avoided"));
    document.SetObject();
    document.AddMember("s", s, allocator);

    std::ostringstream os;
    OStreamWrapper stream(os);
    Writer<OStreamWrapper> writer(stream);
    document.Accept(writer);
    json = os.str();
  }

  EXPECT_EQ("{\"s\":\"terrible\\u0000avoided\"}", json);

  {
    Document document;
    ASSERT_FALSE(document.Parse<0>(json.c_str()).HasParseError());
    EXPECT_EQ(std::string("terrible"), document["s"].GetString());
    EXPECT_EQ(std::string("terrible\0avoided", strlen("terrible") + 1 + strlen("avoided")),
              std::string(document["s"].GetString(), document["s"].GetStringLength()));
  }
}
