/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2016 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

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

#ifndef KARL_TEST_SERVICE_ANNOTATOR_H
#define KARL_TEST_SERVICE_ANNOTATOR_H

#include "schema.h"

#include "../claire.h"

#include "../../Blocks/HTTP/api.h"

#include "../../Sherlock/sherlock.h"

namespace karl_unittest {

class ServiceAnnotator final {
 public:
  ServiceAnnotator(uint16_t port,
                   const std::string& service_generator,
                   const std::string& service_is_prime,
                   const current::karl::Locator& karl)
      : source_numbers_stream_(service_generator + "/numbers"),
        is_prime_logic_endpoint_(service_is_prime + "/is_prime"),
        stream_(current::sherlock::Stream<Number>()),
        http_scope_(HTTP(port).Register("/annotated", stream_)),
        destructing_(false),
        thread_([this]() { Thread(); }),
        claire_(karl, "annotator", port, {service_generator, service_is_prime}) {
#ifdef CURRENT_MOCK_TIME
    // In unit test mode, wait for Karl's response and callback, and fail if Karl is not available.
    claire_.Register(nullptr, true);
#else
    // In example "production" mode just start regular keepalives.
    claire_.Register();
#endif
  }

  ~ServiceAnnotator() {
    destructing_ = true;
    thread_.join();
  }

  const std::string& ClaireCodename() const { return claire_.Codename(); }

 private:
  void Thread() {
    // Poor man's stream subscriber. -- D.K.
    // TODO(dkorolev) + TODO(mzhurovich): Revisit in Thailand as we coin the notion of `HTTPSherlockSusbcriber`.
    int index = 0;
    try {
      while (!destructing_) {
        const auto row = HTTP(GET(source_numbers_stream_ + "?i=" + current::ToString(index++) + "&n=1")).body;
        const auto split = current::strings::Split(row, '\t');
        assert(split.size() == 2u);
        auto number = ParseJSON<Number>(split[1]);
        const auto prime_result =
            HTTP(GET(is_prime_logic_endpoint_ + "?x=" + current::ToString(number.x))).body;
        assert(prime_result == "YES\n" || prime_result == "NO\n");
        number.is_prime = (prime_result == "YES\n");
        stream_.Publish(number);
      }
    } catch (current::net::NetworkException&) {
      // Ignore for the purposes of this test. -- D.K.
    }
  }

  const std::string source_numbers_stream_;
  const std::string is_prime_logic_endpoint_;
  current::sherlock::Stream<Number> stream_;
  const HTTPRoutesScope http_scope_;
  std::atomic_bool destructing_;
  std::thread thread_;
  current::karl::Claire claire_;
};

}  // namespace karl_unittest

#endif  // KARL_TEST_SERVICE_ANNOTATOR_H