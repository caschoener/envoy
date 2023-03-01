#include "test/common/integration/test_engine_builder.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine.h"
#include "library/cc/envoy_error.h"
#include "library/cc/log_level.h"
#include "library/cc/request_headers_builder.h"
#include "library/cc/request_method.h"
#include "library/cc/response_headers.h"

namespace Envoy {
namespace {

const static std::string CONFIG = R"(
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address:
        protocol: TCP
        address: 0.0.0.0
        port_value: 10000
    api_listener:
      api_listener:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
              - name: api
                domains:
                  - "*"
                routes:
                  - match:
                      prefix: "/"
                    direct_response:
                      status: 200
          http_filters:
            - name: envoy.filters.http.assertion
              typed_config:
                "@type": type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion
                match_config:
                  http_request_headers_match:
                    headers:
                      - name: ":authority"
                        exact_match: example.com
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)";

struct Status {
  int status_code;
  bool end_stream;
};

TEST(SendHeadersTest, CanSendHeaders) {
  TestEngineBuilder engine_builder;
  Platform::EngineSharedPtr engine = engine_builder.buildWithString(CONFIG, true);

  Status status;
  absl::Notification stream_complete;
  Platform::StreamSharedPtr stream;
  auto stream_prototype = engine->streamClient()->newStreamPrototype();

  stream_prototype->setOnHeaders(
      [&](Platform::ResponseHeadersSharedPtr headers, bool end_stream, envoy_stream_intel) {
        status.status_code = headers->httpStatus();
        status.end_stream = end_stream;
      });
  stream_prototype->setOnComplete(
      [&](envoy_stream_intel, envoy_final_stream_intel) { stream_complete.Notify(); });
  stream_prototype->setOnError(
      [&](Platform::EnvoyErrorSharedPtr envoy_error, envoy_stream_intel, envoy_final_stream_intel) {
        (void)envoy_error;
        stream_complete.Notify();
      });
  stream_prototype->setOnCancel(
      [&](envoy_stream_intel, envoy_final_stream_intel) { stream_complete.Notify(); });

  stream = stream_prototype->start();

  Platform::RequestHeadersBuilder request_headers_builder(Platform::RequestMethod::GET, "https",
                                                          "example.com", "/");
  auto request_headers = request_headers_builder.build();
  auto request_headers_ptr =
      Platform::RequestHeadersSharedPtr(new Platform::RequestHeaders(request_headers));
  stream->sendHeaders(request_headers_ptr, true);
  stream_complete.WaitForNotification();

  EXPECT_EQ(status.status_code, 200);
  EXPECT_EQ(status.end_stream, true);

  engine->terminate();
}

} // namespace
} // namespace Envoy
