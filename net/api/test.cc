/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2014 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

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

// TODO(dkorolev): Add a 404 test for downloading into file.

// Test for HTTP clients.
// Note that this test relies on HTTP server defined in Bricks.
// Thus, it might have to be tweaked on Windows. TODO(dkorolev): Do it.

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "api.h"
#include "url.h"

#include "../../port.h"

#include "../tcp/tcp.h"

#include "../../dflags/dflags.h"

#include "../../file/file.h"

#include "../../util/make_scope_guard.h"

#include "../../3party/gtest/gtest.h"
#include "../../3party/gtest/gtest-main-with-dflags.h"

using std::chrono::milliseconds;
using std::function;
using std::string;
using std::this_thread::sleep_for;
using std::thread;
using std::to_string;

using bricks::FileSystem;
using bricks::FileException;
using bricks::net::HTTPRedirectNotAllowedException;

using bricks::net::Connection;  // To send HTTP response in chunked transfer encoding.

using namespace bricks::net::api;

DEFINE_bool(test_chunked_encoding,
            true,
            "Whetner the '/drip?numbytes=7' endpoint should use chunked transfer encoding.");

DEFINE_int32(chunked_transfer_delay_between_bytes_ms,
             10,
             "Number of milliseconds to wait between bytes when using chunked encoding.");

#ifndef BRICKS_COVERAGE_REPORT_MODE
TEST(ArchitectureTest, BRICKS_ARCH_UNAME_AS_IDENTIFIER) {
  ASSERT_EQ(BRICKS_ARCH_UNAME, FLAGS_bricks_runtime_arch);
}
#endif

TEST(URLParserTest, SmokeTest) {
  URLParser u;

  u = URLParser("www.google.com");
  EXPECT_EQ("www.google.com", u.host);
  EXPECT_EQ("/", u.path);
  EXPECT_EQ("http", u.protocol);
  EXPECT_EQ(80, u.port);

  u = URLParser("www.google.com/test");
  EXPECT_EQ("www.google.com", u.host);
  EXPECT_EQ("/test", u.path);
  EXPECT_EQ("http", u.protocol);
  EXPECT_EQ(80, u.port);

  u = URLParser("www.google.com:8080");
  EXPECT_EQ("www.google.com", u.host);
  EXPECT_EQ("/", u.path);
  EXPECT_EQ("http", u.protocol);
  EXPECT_EQ(8080, u.port);

  u = URLParser("meh://www.google.com:27960");
  EXPECT_EQ("www.google.com", u.host);
  EXPECT_EQ("/", u.path);
  EXPECT_EQ("meh", u.protocol);
  EXPECT_EQ(27960, u.port);

  u = URLParser("meh://www.google.com:27960/bazinga");
  EXPECT_EQ("www.google.com", u.host);
  EXPECT_EQ("/bazinga", u.path);
  EXPECT_EQ("meh", u.protocol);
  EXPECT_EQ(27960, u.port);

  u = URLParser("localhost:/test");
  EXPECT_EQ("localhost", u.host);
  EXPECT_EQ("/test", u.path);
  EXPECT_EQ("http", u.protocol);
  EXPECT_EQ(80, u.port);
}

TEST(URLParserTest, CompositionTest) {
  EXPECT_EQ("http://www.google.com/", URLParser("www.google.com").ComposeURL());
  EXPECT_EQ("http://www.google.com/", URLParser("http://www.google.com").ComposeURL());
  EXPECT_EQ("http://www.google.com/", URLParser("www.google.com:80").ComposeURL());
  EXPECT_EQ("http://www.google.com/", URLParser("http://www.google.com").ComposeURL());
  EXPECT_EQ("http://www.google.com/", URLParser("http://www.google.com:80").ComposeURL());
  EXPECT_EQ("http://www.google.com:8080/", URLParser("www.google.com:8080").ComposeURL());
  EXPECT_EQ("http://www.google.com:8080/", URLParser("http://www.google.com:8080").ComposeURL());
  EXPECT_EQ("meh://www.google.com:8080/", URLParser("meh://www.google.com:8080").ComposeURL());
}

TEST(URLParserTest, DerivesProtocolFromPreviousPort) {
  // Smoke tests for non-default protocol, setting the 2nd parameter to the URLParser() constructor.
  EXPECT_EQ("www.google.com/", URLParser("www.google.com", "").ComposeURL());
  EXPECT_EQ("telnet://www.google.com:23/", URLParser("www.google.com", "telnet", "", 23).ComposeURL());
  // Keeps the protocol if it was explicitly specified, even for the port that maps to a different protocol.
  EXPECT_EQ("foo://www.google.com:80/", URLParser("foo://www.google.com", "", "", 80).ComposeURL());
  // Maps port 80 into "http://".
  EXPECT_EQ("http://www.google.com/", URLParser("www.google.com", "", "", 80).ComposeURL());
  // Since there is no rule from "23" to "telnet", no protocol is specified.
  EXPECT_EQ("www.google.com:23/", URLParser("www.google.com", "", "", 23).ComposeURL());
}

TEST(URLParserTest, RedirectPreservesProtocolHostAndPortTest) {
  EXPECT_EQ("http://localhost/foo", URLParser("/foo", URLParser("localhost")).ComposeURL());
  EXPECT_EQ("meh://localhost/foo", URLParser("/foo", URLParser("meh://localhost")).ComposeURL());
  EXPECT_EQ("http://localhost:8080/foo", URLParser("/foo", URLParser("localhost:8080")).ComposeURL());
  EXPECT_EQ("meh://localhost:8080/foo", URLParser("/foo", URLParser("meh://localhost:8080")).ComposeURL());
  EXPECT_EQ("meh://localhost:27960/foo",
            URLParser(":27960/foo", URLParser("meh://localhost:8080")).ComposeURL());
  EXPECT_EQ("ftp://foo:8080/", URLParser("ftp://foo", URLParser("meh://localhost:8080")).ComposeURL());
  EXPECT_EQ("ftp://localhost:8080/bar",
            URLParser("ftp:///bar", URLParser("meh://localhost:8080")).ComposeURL());
  EXPECT_EQ("blah://new_host:5000/foo",
            URLParser("blah://new_host/foo", URLParser("meh://localhost:5000")).ComposeURL());
  EXPECT_EQ("blah://new_host:6000/foo",
            URLParser("blah://new_host:6000/foo", URLParser("meh://localhost:5000")).ComposeURL());
}

TEST(URLParserTest, EmptyURLException) {
  // Empty URL or host should throw.
  ASSERT_THROW(URLParser(""), EmptyURLException);
  ASSERT_THROW(URLParser("http://"), EmptyURLHostException);
  ASSERT_THROW(URLParser("http:///foo"), EmptyURLHostException);

  // Empty host is allowed in local links.
  EXPECT_EQ("foo://www.website.com:321/second",
            URLParser("/second", URLParser("foo://www.website.com:321/first")).ComposeURL());
}

// TODO(dkorolev): Migrate to a simpler HTTP server implementation that is to be added to api.h soon.
// This would not require any of these headers.
#include "../http/http.h"
using bricks::net::Socket;
using bricks::net::HTTPServerConnection;
using bricks::net::HTTPHeadersType;
using bricks::net::HTTPResponseCode;

DEFINE_int32(net_api_test_port, 8080, "Local port to use for the test HTTP server.");
DEFINE_string(net_api_test_tmpdir, ".noshit", "Local path for the test to create temporary files in.");

class UseRemoteHTTPBinTestServer_SLOW_TEST_REQUIRING_INTERNET_CONNECTION {
 public:
  struct DummyTypeWithNonTrivialDestructor {
    ~DummyTypeWithNonTrivialDestructor() {
      // To avoid the `unused variable: server_scope` warning down in the tests.
    }
  };
  static string BaseURL() { return "http://httpbin.org"; }
  static DummyTypeWithNonTrivialDestructor SpawnServer() { return DummyTypeWithNonTrivialDestructor{}; }

  // TODO(dkorolev): Get rid of this.
  static bool SupportsExternalURLs() { return true; }
};

class UseLocalHTTPTestServer {
 public:
  static string BaseURL() { return string("http://localhost:") + to_string(FLAGS_net_api_test_port); }

  class ThreadForSingleServerRequest {
   public:
    ThreadForSingleServerRequest(function<void(Socket)> server_impl)
        : server_thread_(server_impl, Socket(FLAGS_net_api_test_port)) {}
    ThreadForSingleServerRequest(ThreadForSingleServerRequest&& rhs)
        : server_thread_(std::move(rhs.server_thread_)) {}
    ~ThreadForSingleServerRequest() { server_thread_.join(); }

   private:
    thread server_thread_;
  };

  static ThreadForSingleServerRequest SpawnServer() {
    return ThreadForSingleServerRequest(UseLocalHTTPTestServer::TestServerHandler);
  }

  // TODO(dkorolev): Get rid of this.
  static bool SupportsExternalURLs() { return false; }

 private:
  // TODO(dkorolev): This code should use our real bricks::HTTPServer, once it's coded.
  static void TestServerHandler(Socket socket) {
    bool serve_more_requests = true;
    while (serve_more_requests) {
      serve_more_requests = false;
      HTTPServerConnection connection(socket.Accept());
      const auto& message = connection.Message();
      const string method = message.Method();
      const string url = message.URL();
      if (method == "GET") {
        if (url == "/get") {
          connection.SendHTTPResponse("DIMA");
        } else if (url == "/drip?numbytes=7") {
          if (!FLAGS_test_chunked_encoding) {
            connection.SendHTTPResponse("*******");  // LCOV_EXCL_LINE
          } else {
            Connection& c = connection.RawConnection();
            c.BlockingWrite("HTTP/1.1 200 OK\r\n");
            c.BlockingWrite("Transfer-Encoding: chunked\r\n");
            c.BlockingWrite("Content-Type: application/octet-stream\r\n");
            c.BlockingWrite("\r\n");
            sleep_for(milliseconds(FLAGS_chunked_transfer_delay_between_bytes_ms));
            for (int i = 0; i < 7; ++i) {
              c.BlockingWrite("1\r\n*\r\n");
              sleep_for(milliseconds(FLAGS_chunked_transfer_delay_between_bytes_ms));
            }
            c.BlockingWrite("0\r\n\r\n");  // Line ending as httpbin.org seems to do it. -- D.K.
          }
          connection.RawConnection().SendEOF();
        } else if (url == "/drip?numbytes=5") {
          connection.SendHTTPResponse("*****");
        } else if (url == "/status/403") {
          connection.SendHTTPResponse("", HTTPResponseCode::Forbidden);
        } else if (url == "/get?Aloha=Mahalo") {
          connection.SendHTTPResponse("{\"Aloha\": \"Mahalo\"}\n");
        } else if (url == "/user-agent") {
          // TODO(dkorolev): Add parsing User-Agent to Bricks' HTTP headers parser.
          connection.SendHTTPResponse("Aloha User Agent");
        } else if (url == "/redirect-to?url=/get") {
          HTTPHeadersType headers;
          headers.push_back(std::make_pair("Location", "/get"));
          connection.SendHTTPResponse("", HTTPResponseCode::Found, "text/html", headers);
          serve_more_requests = true;
        } else {
          ASSERT_TRUE(false) << "GET not implemented for: " << message.URL();  // LCOV_EXCL_LINE
        }
      } else if (method == "POST") {
        if (url == "/post") {
          ASSERT_TRUE(message.HasBody());
          connection.SendHTTPResponse("{\"data\": \"" + message.Body() + "\"}\n");
        } else {
          ASSERT_TRUE(false) << "POST not implemented for: " << message.URL();  // LCOV_EXCL_LINE
        }
      } else {
        ASSERT_TRUE(false) << "Method not implemented: " << message.Method();  // LCOV_EXCL_LINE
      }
    }
  }
};

template <typename T>
class HTTPClientTemplatedTest : public ::testing::Test {};

typedef ::testing::Types<UseLocalHTTPTestServer
#ifndef BRICKS_COVERAGE_REPORT_MODE
                         ,
                         UseRemoteHTTPBinTestServer_SLOW_TEST_REQUIRING_INTERNET_CONNECTION
#endif
                         > HTTPClientTestTypeList;
TYPED_TEST_CASE(HTTPClientTemplatedTest, HTTPClientTestTypeList);

TYPED_TEST(HTTPClientTemplatedTest, GetToBuffer) {
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/drip?numbytes=7";
  const auto response = HTTP(GET(url));
  EXPECT_EQ(200, static_cast<int>(response.code));
  EXPECT_EQ("*******", response.body);
  EXPECT_EQ(url, response.url);
}

TYPED_TEST(HTTPClientTemplatedTest, GetToFile) {
  bricks::FileSystem::CreateDirectory(FLAGS_net_api_test_tmpdir, FileSystem::CreateDirectoryParameters::Silent);
  const string file_name = FLAGS_net_api_test_tmpdir + "/some_test_file_for_http_get";
  const auto test_file_scope = FileSystem::ScopedRemoveFile(file_name);
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/drip?numbytes=5";
  const auto response = HTTP(GET(url), SaveResponseToFile(file_name));
  EXPECT_EQ(200, static_cast<int>(response.code));
  EXPECT_EQ(file_name, response.body_file_name);
  EXPECT_EQ(url, response.url);
  EXPECT_EQ("*****", FileSystem::ReadFileAsString(response.body_file_name));
}

TYPED_TEST(HTTPClientTemplatedTest, PostFromBufferToBuffer) {
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/post";
  const string post_body = "Hello, World!";
  const auto response = HTTP(POST(url, post_body, "application/octet-stream"));
  EXPECT_NE(string::npos, response.body.find("\"data\": \"" + post_body + "\"")) << response.body;
}

TYPED_TEST(HTTPClientTemplatedTest, PostFromInvalidFile) {
  bricks::FileSystem::CreateDirectory(FLAGS_net_api_test_tmpdir, FileSystem::CreateDirectoryParameters::Silent);
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/post";
  const string non_existent_file_name = FLAGS_net_api_test_tmpdir + "/non_existent_file";
  const auto test_file_scope = FileSystem::ScopedRemoveFile(non_existent_file_name);
  ASSERT_THROW(HTTP(POSTFromFile(url, non_existent_file_name, "text/plain")), FileException);
  // Still do one request since local HTTP server is waiting for it.
  EXPECT_EQ(200, static_cast<int>(HTTP(GET(TypeParam::BaseURL() + "/get")).code));
}

TYPED_TEST(HTTPClientTemplatedTest, PostFromFileToBuffer) {
  bricks::FileSystem::CreateDirectory(FLAGS_net_api_test_tmpdir, FileSystem::CreateDirectoryParameters::Silent);
  const string file_name = FLAGS_net_api_test_tmpdir + "/some_input_test_file_for_http_post";
  const auto test_file_scope = FileSystem::ScopedRemoveFile(file_name);
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/post";
  FileSystem::WriteStringToFile(file_name.c_str(), file_name);
  const auto response = HTTP(POSTFromFile(url, file_name, "application/octet-stream"));
  EXPECT_EQ(200, static_cast<int>(response.code));
  EXPECT_NE(string::npos, response.body.find(file_name));
}

TYPED_TEST(HTTPClientTemplatedTest, PostFromBufferToFile) {
  bricks::FileSystem::CreateDirectory(FLAGS_net_api_test_tmpdir, FileSystem::CreateDirectoryParameters::Silent);
  const string file_name = FLAGS_net_api_test_tmpdir + "/some_output_test_file_for_http_post";
  const auto test_file_scope = FileSystem::ScopedRemoveFile(file_name);
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/post";
  const auto response = HTTP(POST(url, "TEST BODY", "text/plain"), SaveResponseToFile(file_name));
  EXPECT_EQ(200, static_cast<int>(response.code));
  EXPECT_NE(string::npos, FileSystem::ReadFileAsString(response.body_file_name).find("TEST BODY"));
}

TYPED_TEST(HTTPClientTemplatedTest, PostFromFileToFile) {
  bricks::FileSystem::CreateDirectory(FLAGS_net_api_test_tmpdir, FileSystem::CreateDirectoryParameters::Silent);
  const string request_file_name = FLAGS_net_api_test_tmpdir + "/some_complex_request_test_file_for_http_post";
  const string response_file_name =
      FLAGS_net_api_test_tmpdir + "/some_complex_response_test_file_for_http_post";
  const auto input_file_scope = FileSystem::ScopedRemoveFile(request_file_name);
  const auto output_file_scope = FileSystem::ScopedRemoveFile(response_file_name);
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/post";
  const string post_body = "Aloha, this text should pass from one file to another. Mahalo!";
  FileSystem::WriteStringToFile(request_file_name.c_str(), post_body);
  const auto response =
      HTTP(POSTFromFile(url, request_file_name, "text/plain"), SaveResponseToFile(response_file_name));
  EXPECT_EQ(200, static_cast<int>(response.code));
  {
    const string received_data = FileSystem::ReadFileAsString(response.body_file_name);
    EXPECT_NE(string::npos, received_data.find(post_body)) << received_data << "\n" << post_body;
  }
}

TYPED_TEST(HTTPClientTemplatedTest, ErrorCodes) {
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/status/403";
  EXPECT_EQ(403, static_cast<int>(HTTP(GET(url)).code));
}

TYPED_TEST(HTTPClientTemplatedTest, SendsURLParameters) {
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/get?Aloha=Mahalo";
  const auto response = HTTP(GET(url));
  EXPECT_EQ(url, response.url);
  EXPECT_EQ(200, static_cast<int>(response.code));
  EXPECT_NE(string::npos, response.body.find("\"Aloha\": \"Mahalo\""));
}

TYPED_TEST(HTTPClientTemplatedTest, HttpRedirect302NotAllowedByDefault) {
  const auto server_scope = TypeParam::SpawnServer();
  ASSERT_THROW(HTTP(GET(TypeParam::BaseURL() + "/redirect-to?url=/get")), HTTPRedirectNotAllowedException);
}

TYPED_TEST(HTTPClientTemplatedTest, HttpRedirect302Allowed) {
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/redirect-to?url=/get";
  const auto response = HTTP(GET(url).AllowRedirects());
  EXPECT_EQ(200, static_cast<int>(response.code));
  EXPECT_NE(url, response.url);
  EXPECT_EQ(TypeParam::BaseURL() + "/get", response.url);
}

TYPED_TEST(HTTPClientTemplatedTest, UserAgent) {
  const auto server_scope = TypeParam::SpawnServer();
  const string url = TypeParam::BaseURL() + "/user-agent";
  const string custom_user_agent = "Aloha User Agent";
  const auto response = HTTP(GET(url).UserAgent(custom_user_agent));
  EXPECT_EQ(url, response.url);
  EXPECT_EQ(200, static_cast<int>(response.code));
  EXPECT_NE(string::npos, response.body.find(custom_user_agent));
}

// LCOV_EXCL_START

// TODO(dkorolev): Get rid of the tests involving external URLs.
// TODO(dkorolev): This test is now failing on my client implementation. Fix it.
TYPED_TEST(HTTPClientTemplatedTest, DISABLED_HttpRedirect301) {
  if (TypeParam::SupportsExternalURLs()) {
    const auto response = HTTP(GET("http://github.com"));
    EXPECT_EQ(200, static_cast<int>(response.code));
    EXPECT_EQ("https://github.com/", response.url);
  }
}

// TODO(dkorolev): This test is now timing out on my client implementation. Fix it.
TYPED_TEST(HTTPClientTemplatedTest, DISABLED_HttpRedirect307) {
  if (TypeParam::SupportsExternalURLs()) {
    const auto response = HTTP(GET("http://msn.com"));
    EXPECT_EQ(200, static_cast<int>(response.code));
    EXPECT_EQ("http://www.msn.com/", response.url);
  }
}

TYPED_TEST(HTTPClientTemplatedTest, InvalidUrl) {
  if (TypeParam::SupportsExternalURLs()) {
    try {
      // TODO(dkorolev): Change this to ASSERT_THROW().
      const auto response = HTTP(GET("http://very.bad.url/that/will/not/load"));
      EXPECT_NE(200, static_cast<int>(response.code));
    } catch (bricks::net::SocketResolveAddressException&) {
    }
  }
}

// LCOV_EXCL_STOP