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

#ifndef BRICKS_NET_HTTP_IMPL_SERVER_H
#define BRICKS_NET_HTTP_IMPL_SERVER_H

#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

#include "../codes.h"
#include "../mime_type.h"
#include "../default_messages.h"

#include "../headers/headers.h"

#include "../../exceptions.h"

#include "../../tcp/tcp.h"

#include "../../../template/enable_if.h"

#include "../../../../TypeSystem/struct.h"
#include "../../../../TypeSystem/Serialization/json.h"

#include "../../../strings/util.h"
#include "../../../strings/split.h"

#include "../../../../Blocks/URL/url.h"

namespace current {
namespace net {

// HTTP constants to parse the header and extract method, URL, headers and body.
// TODO(dkorolev): Move these constants away from this `.../impl/...` file.
namespace constants {

constexpr static const char kCRLF[] = "\r\n";
constexpr const size_t kCRLFLength = strings::CompileTimeStringLength(kCRLF);

constexpr const char* kDefaultContentType = "text/plain";
constexpr const char* kDefaultJSONContentType = "application/json; charset=utf-8";
// TODO(dkorolev): Make use of this constant everywhere.
constexpr const char* kDefaultHTMLContentType = "text/html; charset=utf-8";

constexpr const char kHeaderKeyValueSeparator[] = ": ";
constexpr const size_t kHeaderKeyValueSeparatorLength =
    strings::CompileTimeStringLength(kHeaderKeyValueSeparator);

constexpr const char* const kContentLengthHeaderKey = "Content-Length";
constexpr const char* const kTransferEncodingHeaderKey = "Transfer-Encoding";
constexpr const char* const kTransferEncodingChunkedValue = "chunked";

inline static const http::Headers DefaultJSONHTTPHeaders() {
  return http::Headers({{"Access-Control-Allow-Origin", "*"}});
}

}  // namespace constants

// HTTPDefaultHelper handles headers and chunked transfers.
// One can inject a custom implementaion of it to avoid keeping all HTTP body in memory.
// TODO(dkorolev): This is not yet the case, but will be soon once I fix HTTP parse code.
class HTTPDefaultHelper {
 public:
  struct ConstructionParams {};
  HTTPDefaultHelper(const ConstructionParams&) {}

  const http::Headers& headers() const { return headers_; }

 protected:
  HTTPDefaultHelper() = default;

  inline void OnHeader(const char* key, const char* value) { headers_.SetHeaderOrCookie(key, value); }

  inline void OnChunk(const char* chunk, size_t length) { body_.append(chunk, length); }

  inline void OnChunkedBodyDone(const char*& begin, const char*& end) {
    begin = body_.data();
    end = begin + body_.length();
  }

 private:
  http::Headers headers_;
  std::string body_;
};

// In constructor, GenericHTTPRequestData parses HTTP response from `Connection&` is was provided with.
// Extracts method, path (URL + parameters), and, if provided, the body.
//
// Getters:
// * current::url::URL URL() (to access `.host`, `.path`, `.scheme` and `.port`).
// * std::string RawPath() (the URL before parsing).
// * std::string Method().
// * std::string Body(), size_t BodyLength(), const char* Body{Begin,End}().
//
// Exceptions:
// * ConnectionResetByPeer       : When the server is using chunked transfer and doesn't fully send one.
//
// HTTP message: http://www.w3.org/Protocols/rfc2616/rfc2616.html
template <class HELPER>
class GenericHTTPRequestData : public HELPER {
 public:
  inline GenericHTTPRequestData(
      Connection& c,
      const typename HELPER::ConstructionParams& params = typename HELPER::ConstructionParams(),
      const int intial_buffer_size = 1600,
      const double buffer_growth_k = 1.95,
      const size_t buffer_max_growth_due_to_content_length = 1024 * 1024)
      : HELPER(params), buffer_(intial_buffer_size) {
    // `offset` is the number of bytes read into `buffer_` so far.
    // `length_cap` is infinity first (size_t is unsigned), and it changes/ to the absolute offset
    // of the end of HTTP body in the buffer_, once `Content-Length` and two consecutive CRLS have been seen.
    size_t offset = 0;
    size_t length_cap = static_cast<size_t>(-1);

    // `current_line_offset` is the index of the first character after CRLF in `buffer_`.
    size_t current_line_offset = 0;

    // `body_offset` and `body_length` describe the position of HTTP body, if it's not chunk-encoded.
    size_t body_offset = static_cast<size_t>(-1);
    size_t body_length = static_cast<size_t>(-1);

    // `first_line_parsed` denotes whether the line being parsed is the first one, with method and URL.
    bool first_line_parsed = false;

    // `chunked_transfer_encoding` is set when body should be received in chunks insted of a single read.
    bool chunked_transfer_encoding = false;

    // `receiving_body_in_chunks` is set to true when the parsing is already in the "receive body" mode.
    bool receiving_body_in_chunks = false;

    while (offset < length_cap) {
      size_t chunk;
      size_t read_count;
      // Use `- offset - 1` instead of just `- offset` to leave room for the '\0'.
      assert(buffer_.size() > offset + 1);
      // NOTE: This `if` should not be made a `while`, as it may so happen that the boundary between two
      // consecutively received packets lays right on the final size, but instead of parsing the received body,
      // the server would wait forever for more data to arrive from the client.
      if (chunk = buffer_.size() - offset - 1,
          read_count = c.BlockingRead(&buffer_[offset], chunk),
          offset += read_count,
          read_count == chunk && offset < length_cap) {
        // The `std::max()` condition is kept just in case we compile Current for a device
        // that is extremely short on memory, for which `buffer_growth_k` could be some 1.0001. -- D.K.
        buffer_.resize(std::max(static_cast<size_t>(buffer_.size() * buffer_growth_k), offset + 2));
      }
      if (!read_count) {
        // This is worth re-checking, but as for 2014/12/06 the concensus of reading through man
        // and StackOverflow is that a return value of zero from read() from a socket indicates
        // that the socket has been closed by the peer.
        CURRENT_THROW(ConnectionResetByPeer());  // LCOV_EXCL_LINE
      }
      buffer_[offset] = '\0';
      char* next_crlf_ptr;
      while ((body_offset == static_cast<size_t>(-1) || offset < body_offset) &&
             (next_crlf_ptr = strstr(&buffer_[current_line_offset], constants::kCRLF))) {
        const bool line_is_blank = (next_crlf_ptr == &buffer_[current_line_offset]);
        *next_crlf_ptr = '\0';
        // `next_line_offset` is mutable since reading chunked body will change it.
        size_t next_line_offset = next_crlf_ptr + constants::kCRLFLength - &buffer_[0];
        if (!first_line_parsed) {
          if (!line_is_blank) {
            // It's recommended by W3 to wait for the first line ignoring prior CRLF-s.
            const std::vector<std::string> pieces =
                strings::Split<strings::ByWhitespace>(&buffer_[current_line_offset]);
            if (pieces.size() >= 1) {
              method_ = pieces[0];
            }
            if (pieces.size() >= 2) {
              raw_path_ = pieces[1];
              url_ = current::url::URL(raw_path_);
            }
            first_line_parsed = true;
          }
        } else if (receiving_body_in_chunks) {
          // Ignore blank lines.
          if (!line_is_blank) {
            const size_t chunk_length =
                static_cast<size_t>(std::stoi(&buffer_[current_line_offset], nullptr, 16));
            if (chunk_length == 0) {
              // Done with the body.
              HELPER::OnChunkedBodyDone(body_buffer_begin_, body_buffer_end_);
              return;
            } else {
              // A chunk of length `chunk_length` bytes starts right at next_line_offset.
              const size_t chunk_offset = next_line_offset;
              // First, make sure it has been read.
              const size_t next_offset = chunk_offset + chunk_length;
              if (offset < next_offset) {
                const size_t bytes_to_read = next_offset - offset;
                // The very minimum for this condition is `buffer_.size() < next_offset + 2`:
                // a) plus one is required for the padding `\0`, and
                // b) another plus one is required to have room to read at least one more byte
                //    during the next iteration of the outer loop.
                // The original version of this code was only adding one to `next_offset`.
                // This had a bug, which got revealed as the `while` loop above has been corrected into `if`.
                // Upon changing the `while` to an `if`, the `assert (buffer_.size() > offset + 1);` check above
                // would fail on a chunked HTTP body of several large chunks. Thus, `next_offset + 2` is it.
                // Note that the actual `resize()` would always allocate more room than the extra two bytes.
                // The `std::max()` condition is kept just in case we compile Current for a device
                // that is extremely short on memory, for which `buffer_growth_k` could be some 1.0001. -- D.K.
                if (buffer_.size() < next_offset + 2) {
                  // LCOV_EXCL_START
                  // TODO(dkorolev): See if this can be tested better; now the test for these lines is flaky.
                  buffer_.resize(
                      std::max(static_cast<size_t>(buffer_.size() * buffer_growth_k), next_offset + 2));
                  // LCOV_EXCL_STOP
                }
                if (bytes_to_read !=
                    c.BlockingRead(&buffer_[offset], bytes_to_read, Connection::FillFullBuffer)) {
                  CURRENT_THROW(ConnectionResetByPeer());  // LCOV_EXCL_LINE
                }
                offset = next_offset;
              }
              // Then, append this newly parsed or received chunk to the body.
              HELPER::OnChunk(&buffer_[chunk_offset], chunk_length);
              // Finally, change `next_line_offset` to force skipping the, possibly binary, body.
              // There will be an extra CRLF after the chunk, but we don't require it.
              next_line_offset = next_offset;
              // TODO(dkorolev): The above code works, but keeps growing memory usage. Shrink it.
            }
          }
        } else if (!line_is_blank) {
          char* p = strstr(&buffer_[current_line_offset], constants::kHeaderKeyValueSeparator);
          if (p) {
            *p = '\0';
            const char* const key = &buffer_[current_line_offset];
            const char* const value = p + constants::kHeaderKeyValueSeparatorLength;
            HELPER::OnHeader(key, value);
            if (!strcmp(key, constants::kContentLengthHeaderKey)) {
              body_length = static_cast<size_t>(atoi(value));
            } else if (!strcmp(key, constants::kTransferEncodingHeaderKey)) {
              if (!strcmp(value, constants::kTransferEncodingChunkedValue)) {
                chunked_transfer_encoding = true;
              }
            }
          }
        } else {
          if (!chunked_transfer_encoding) {
            // HTTP body starts right after this last CRLF.
            body_offset = next_line_offset;
            // Non-chunked encoding. Assume BODY follows as raw data.
            // Only accept HTTP body if Content-Length has been set; ignore it otherwise.
            if (body_length != static_cast<size_t>(-1)) {
              // Has HTTP body to parse.
              length_cap = body_offset + body_length;
              // Resize the buffer to be able to get the contents of HTTP body without extra resizes,
              // while being careful to not be open to extra-large mistakenly or maliciously set
              // Content-Length.
              // Keep in mind that `buffer_` should have the size of `length_cap + 1`, to include the `\0'.
              if (length_cap + 1 > buffer_.size()) {
                const size_t delta_size = length_cap + 1 - buffer_.size();
                buffer_.resize(buffer_.size() + std::min(delta_size, buffer_max_growth_due_to_content_length));
              }
            } else {
              // Indicate we are done parsing the header.
              length_cap = body_offset;
            }
          } else {
            receiving_body_in_chunks = true;
          }
        }
        current_line_offset = next_line_offset;
      }
    }
    if (body_length != static_cast<size_t>(-1)) {
      // Initialize pointers pair to point to the BODY to be read.
      body_buffer_begin_ = &buffer_[body_offset];
      body_buffer_end_ = body_buffer_begin_ + body_length;
    }
  }

  inline const std::string& Method() const { return method_; }
  inline const current::url::URL& URL() const { return url_; }
  inline const std::string& RawPath() const { return raw_path_; }

  // Note that `Body*()` methods assume that the body was fully read into memory.
  // If other means of reading the body, for example, event-based chunk parsing, is used,
  // then `Body()` will return empty string and all other `Body*()` methods will return nullptr.

  inline const std::string& Body() const {
    if (!prepared_body_) {
      if (body_buffer_begin_) {
        prepared_body_.reset(new std::string(body_buffer_begin_, body_buffer_end_));
      } else {
        prepared_body_.reset(new std::string());
      }
    }
    return *prepared_body_.get();
  }

  inline const char* BodyBegin() const { return body_buffer_begin_; }

  inline const char* BodyEnd() const { return body_buffer_end_; }

  inline size_t BodyLength() const {
    if (body_buffer_begin_) {
      assert(body_buffer_end_);
      return body_buffer_end_ - body_buffer_begin_;
    } else {
      return 0u;
    }
  }

 private:
  // Fields available to the user via getters.
  std::string method_;
  current::url::URL url_;
  std::string raw_path_;

  // HTTP parsing fields that have to be caried out of the parsing routine.
  std::vector<char> buffer_;  // The buffer into which data has been read, except for chunked case.
  const char* body_buffer_begin_ = nullptr;  // If BODY has been provided, pointer pair to it.
  const char* body_buffer_end_ = nullptr;    // Will not be nullptr if body_buffer_begin_ is not nullptr.

  // HTTP body gets converted to an std::string representation as it's first requested.
  // TODO(dkorolev): This pattern is worth revisiting. StringPiece?
  mutable std::unique_ptr<std::string> prepared_body_;

  // Disable any copy/move support since this class uses pointers.
  GenericHTTPRequestData() = delete;
  GenericHTTPRequestData(const GenericHTTPRequestData&) = delete;
  GenericHTTPRequestData(GenericHTTPRequestData&&) = delete;
  void operator=(const GenericHTTPRequestData&) = delete;
  void operator=(GenericHTTPRequestData&&) = delete;
};

// The default implementation is exposed as HTTPRequestData.
using HTTPRequestData = GenericHTTPRequestData<HTTPDefaultHelper>;

template <class HTTP_REQUEST_DATA>
class GenericHTTPServerConnection final {
 public:
  typedef enum { ConnectionClose, ConnectionKeepAlive } ConnectionType;
  // The only constructor parses HTTP headers coming from the socket
  // in the constructor of `message_(connection_)`.
  GenericHTTPServerConnection(Connection&& c,
                              const typename HTTP_REQUEST_DATA::ConstructionParams& params =
                                  typename HTTP_REQUEST_DATA::ConstructionParams())
      : connection_(std::move(c)), message_(connection_, params) {}
  ~GenericHTTPServerConnection() {
    if (!responded_) {
      // If a user code throws an exception in a different thread, it will not be caught.
      // But, at least, capitalized "INTERNAL SERVER ERROR" will be returned.
      // It's also a good place for a breakpoint to tell the source of that exception.
      // LCOV_EXCL_START
      try {
        SendHTTPResponse(
            DefaultInternalServerErrorMessage(), HTTPResponseCode.InternalServerError, "text/html");
      } catch (const Exception& e) {
        // No exception should ever leave the destructor.
        if (message_.RawPath() == "/healthz") {
          // Report nothing for "/healthz", since it's an internal URL, also used by the tests
          // to poke the serving thread before shutting down the server. There is nothing exceptional
          // with not responding to "/healthz", really -- it just means that the server is not healthy, duh. --
          // D.K.
        } else {
          std::cerr << "An exception occurred while trying to send \"INTERNAL SERVER ERROR\"\n";
          std::cerr << "In: " << message_.Method() << ' ' << message_.RawPath() << std::endl;
          std::cerr << e.what() << std::endl;
        }
      }
      // LCOV_EXCL_STOP
    }
  }

  inline static void PrepareHTTPResponseHeader(std::ostream& os,
                                               ConnectionType connection_type,
                                               HTTPResponseCodeValue code = HTTPResponseCode.OK,
                                               const std::string& content_type = constants::kDefaultContentType,
                                               const http::Headers& extra_headers = http::Headers()) {
    os << "HTTP/1.1 " << static_cast<int>(code);
    os << " " << HTTPResponseCodeAsString(code) << constants::kCRLF;
    os << "Content-Type: " << content_type << constants::kCRLF;
    os << "Connection: " << (connection_type == ConnectionKeepAlive ? "keep-alive" : "close")
       << constants::kCRLF;
    for (const auto& cit : extra_headers) {
      os << cit.header << ": " << cit.value << constants::kCRLF;
    }
    for (const auto& cit : extra_headers.cookies) {
      os << "Set-Cookie: " << cit.first << '=' << cit.second.value;
      for (const auto& cit2 : cit.second.params) {
        os << "; " << cit2.first;
        if (!cit2.second.empty()) {
          os << '=' + cit2.second;
        }
      }
      os << constants::kCRLF;
    }
  }

  // The actual implementation of sending the HTTP response.
  template <typename T>
  inline void SendHTTPResponseImpl(const T& begin,
                                   const T& end,
                                   HTTPResponseCodeValue code,
                                   const std::string& content_type,
                                   const http::Headers& extra_headers) {
    if (responded_) {
      CURRENT_THROW(AttemptedToSendHTTPResponseMoreThanOnce());
    } else {
      responded_ = true;
      std::ostringstream os;
      PrepareHTTPResponseHeader(os, ConnectionClose, code, content_type, extra_headers);
      os << "Content-Length: " << (end - begin) << constants::kCRLF << constants::kCRLF;
      connection_.BlockingWrite(os.str(), true);
      connection_.BlockingWrite(begin, end, false);
    }
  }

  // Only support STL containers of chars and bytes, this does not yet cover std::string.
  template <typename T>
  inline ENABLE_IF<sizeof(typename T::value_type) == 1> SendHTTPResponse(
      const T& begin,
      const T& end,
      HTTPResponseCodeValue code = HTTPResponseCode.OK,
      const std::string& content_type = constants::kDefaultContentType,
      const http::Headers& extra_headers = http::Headers()) {
    SendHTTPResponseImpl(begin, end, code, content_type, extra_headers);
  }
  template <typename T>
  inline ENABLE_IF<sizeof(typename T::value_type) == 1> SendHTTPResponse(
      T&& container,
      HTTPResponseCodeValue code = HTTPResponseCode.OK,
      const std::string& content_type = constants::kDefaultContentType,
      const http::Headers& extra_headers = http::Headers()) {
    SendHTTPResponseImpl(container.begin(), container.end(), code, content_type, extra_headers);
  }

  // Special case to handle std::string.
  inline void SendHTTPResponse(const std::string& string,
                               HTTPResponseCodeValue code = HTTPResponseCode.OK,
                               const std::string& content_type = constants::kDefaultContentType,
                               const http::Headers& extra_headers = http::Headers()) {
    SendHTTPResponseImpl(string.begin(), string.end(), code, content_type, extra_headers);
  }

  // Support `CURRENT_STRUCT`-s.
  template <class T>
  inline ENABLE_IF<IS_CURRENT_STRUCT(current::decay<T>)> SendHTTPResponse(
      T&& object,
      HTTPResponseCodeValue code = HTTPResponseCode.OK,
      const std::string& content_type = constants::kDefaultJSONContentType,
      const http::Headers& extra_headers = constants::DefaultJSONHTTPHeaders()) {
    // TODO(dkorolev): We should probably make this not only correct but also efficient.
    const std::string s = JSON(std::forward<T>(object)) + '\n';
    SendHTTPResponseImpl(s.begin(), s.end(), code, content_type, extra_headers);
  }

  // Support `CURRENT_STRUCT`-s wrapper under a user-defined name.
  // (For backwards compatibility only, really. -- D.K.)
  template <class T>
  inline ENABLE_IF<IS_CURRENT_STRUCT(current::decay<T>)> SendHTTPResponse(
      T&& object,
      const std::string& name,
      HTTPResponseCodeValue code = HTTPResponseCode.OK,
      const std::string& content_type = constants::kDefaultJSONContentType,
      const http::Headers& extra_headers = constants::DefaultJSONHTTPHeaders()) {
    // TODO(dkorolev): We should probably make this not only correct but also efficient.
    const std::string s = "{\"" + name + "\":" + JSON(std::forward<T>(object)) + "}\n";
    SendHTTPResponseImpl(s.begin(), s.end(), code, content_type, extra_headers);
  }

  // The wrapper to send HTTP response in chunks.
  struct ChunkedResponseSender final {
    // `struct Impl` is the logic wrapped into an `std::unique_ptr<>` to call the destructor only once.
    struct Impl final {
      explicit Impl(Connection& connection) : connection_(connection) {}

      ~Impl() {
        if (!can_no_longer_write_) {
          try {
            connection_.BlockingWrite("0", true);
            // Should send CRLF twice.
            connection_.BlockingWrite(constants::kCRLF, true);
            connection_.BlockingWrite(constants::kCRLF, false);
          } catch (const SocketException& e) {                                          // LCOV_EXCL_LINE
            std::cerr << "Chunked response closure failed: " << e.what() << std::endl;  // LCOV_EXCL_LINE
          }                                                                             // LCOV_EXCL_LINE
        }
      }

      // The actual implementation of sending HTTP chunk data.
      template <typename T>
      void SendImpl(T&& data) {
        if (!data.empty()) {
          try {
            connection_.BlockingWrite(strings::Printf("%X", data.size()), true);
            connection_.BlockingWrite(constants::kCRLF, true);
            connection_.BlockingWrite(std::forward<T>(data), true);
            // Force every chunk to be sent out by passing `false` as the second argument.
            connection_.BlockingWrite(constants::kCRLF, false);
          } catch (const SocketException&) {
            // For chunked HTTP responses, if the receiving end has closed the connection,
            // as detected during `Send`, surpass logging about the failure to send the final "zero" chunk.
            can_no_longer_write_ = true;
            throw;
          }
        }
      }

      // Only support STL containers of chars and bytes, this does not yet cover std::string.
      template <typename T>
      inline ENABLE_IF<sizeof(typename T::value_type) == 1> Send(T&& data) {
        SendImpl(std::forward<T>(data));
      }

      // Special case to handle std::string.
      inline void Send(const std::string& data) { SendImpl(data); }

      // Support `CURRENT_STRUCT`-s.
      template <class T>
      inline ENABLE_IF<IS_CURRENT_STRUCT(current::decay<T>)> Send(T&& object) {
        SendImpl(JSON(std::forward<T>(object)) + '\n');
      }
      template <class T, typename S>
      inline ENABLE_IF<IS_CURRENT_STRUCT(current::decay<T>)> Send(T&& object, S&& name) {
        SendImpl(std::string("{\"") + name + "\":" + JSON(std::forward<T>(object)) + "}\n");
      }

      Connection& connection_;
      bool can_no_longer_write_ = false;

      Impl() = delete;
      Impl(const Impl&) = delete;
      Impl(Impl&&) = delete;
      void operator=(const Impl&) = delete;
      void operator=(Impl&&) = delete;
    };

    explicit ChunkedResponseSender(Connection& connection) : impl_(new Impl(connection)) {}

    template <typename T>
    inline ChunkedResponseSender& Send(T&& data) {
      impl_->Send(std::forward<T>(data));
      return *this;
    }

    template <typename T1, typename T2>
    inline ChunkedResponseSender& Send(T1&& data1, T2&& data2) {
      impl_->Send(std::forward<T1>(data1), std::forward<T2>(data2));
      return *this;
    }

    template <typename T>
    inline ChunkedResponseSender& operator()(T&& data) {
      impl_->Send(std::forward<T>(data));
      return *this;
    }

    template <typename T1, typename T2>
    inline ChunkedResponseSender& operator()(T1&& data1, T2&& data2) {
      impl_->Send(std::forward<T1>(data1), std::forward<T2>(data2));
      return *this;
    }

    std::unique_ptr<Impl> impl_;
  };

  inline ChunkedResponseSender SendChunkedHTTPResponse(
      HTTPResponseCodeValue code = HTTPResponseCode.OK,
      const std::string& content_type = constants::kDefaultJSONContentType,
      const http::Headers& extra_headers = constants::DefaultJSONHTTPHeaders()) {
    if (responded_) {
      CURRENT_THROW(AttemptedToSendHTTPResponseMoreThanOnce());
    } else {
      responded_ = true;
      std::ostringstream os;
      PrepareHTTPResponseHeader(os, ConnectionKeepAlive, code, content_type, extra_headers);
      os << "Transfer-Encoding: chunked" << constants::kCRLF << constants::kCRLF;
      connection_.BlockingWrite(os.str(), true);
      return ChunkedResponseSender(connection_);
    }
  }

  // To allow for a clean shutdown, without throwing an exception
  // that a response, that does not have to be sent, was really not sent.
  inline void DoNotSendAnyResponse() {
    if (responded_) {
      CURRENT_THROW(AttemptedToSendHTTPResponseMoreThanOnce());  // LCOV_EXCL_LINE
    }
    responded_ = true;
  }

  const GenericHTTPRequestData<HTTP_REQUEST_DATA>& HTTPRequest() const { return message_; }

  const IPAndPort& LocalIPAndPort() const { return connection_.LocalIPAndPort(); }
  const IPAndPort& RemoteIPAndPort() const { return connection_.RemoteIPAndPort(); }

  Connection& RawConnection() { return connection_; }

 private:
  bool responded_ = false;
  Connection connection_;
  GenericHTTPRequestData<HTTP_REQUEST_DATA> message_;

  // Disable any copy/move support for extra safety.
  GenericHTTPServerConnection(const GenericHTTPServerConnection&) = delete;
  GenericHTTPServerConnection(const Connection&) = delete;
  GenericHTTPServerConnection(GenericHTTPServerConnection&&) = delete;
  // The only legit constructor is `GenericHTTPServerConnection(Connection&&)`.
  void operator=(const Connection&) = delete;
  void operator=(const GenericHTTPServerConnection&) = delete;
  void operator=(Connection&&) = delete;
  void operator=(GenericHTTPServerConnection&&) = delete;
};

using HTTPServerConnection = GenericHTTPServerConnection<HTTPDefaultHelper>;

}  // namespace net
}  // namespace current

#endif  // BRICKS_NET_HTTP_IMPL_SERVER_H
