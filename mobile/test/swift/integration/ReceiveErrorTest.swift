import Envoy
import EnvoyEngine
import Foundation
import TestExtensions
import XCTest

final class ReceiveErrorTests: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testReceiveError() {
    // swiftlint:disable:next line_length
    let emhcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
    // swiftlint:disable:next line_length
    let pbfType = "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge"
    // swiftlint:disable:next line_length
    let localErrorFilterType = "type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError"
    let filterName = "error_validation_filter"
    let config =
"""
listener_manager:
    name: envoy.listener_manager_impl.api
    typed_config:
      "@type": type.googleapis.com/envoy.config.listener.v3.ApiListenerManager
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": \(emhcmType)
        config:
          stat_prefix: hcm
          route_config:
            name: api_router
            virtual_hosts:
            - name: api
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                direct_response: { status: 503 }
          http_filters:
          - name: envoy.filters.http.platform_bridge
            typed_config:
              "@type": \(pbfType)
              platform_filter_name: \(filterName)
          - name: envoy.filters.http.local_error
            typed_config:
              "@type": \(localErrorFilterType)
          - name: envoy.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
"""

    struct ErrorValidationFilter: ResponseFilter {
      let receivedError: XCTestExpectation
      let notCancelled: XCTestExpectation

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        return .continue(headers: headers)
      }

      func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<ResponseHeaders>
      {
        return .continue(data: body)
      }

      func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
        return .continue(trailers: trailers)
      }

      func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel) {
        XCTAssertEqual(error.errorCode, 2) // 503/Connection Failure
        self.receivedError.fulfill()
      }

      func onCancel(streamIntel: FinalStreamIntel) {
        XCTFail("Unexpected call to onCancel filter callback")
        self.notCancelled.fulfill()
      }

      func onComplete(streamIntel: FinalStreamIntel) {}
    }

    let callbackReceivedError = self.expectation(description: "Run called with expected error")
    let filterReceivedError = self.expectation(description: "Filter called with expected error")
    let filterNotCancelled =
      self.expectation(description: "Filter called with unexpected cancellation")
    filterNotCancelled.isInverted = true
    let expectations = [filterReceivedError, filterNotCancelled, callbackReceivedError]

    let engine = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .addPlatformFilter(
        name: filterName,
        factory: {
          ErrorValidationFilter(receivedError: filterReceivedError,
                                notCancelled: filterNotCancelled)
        }
      )
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "example.com", path: "/test")
      .build()

    client
      .newStreamPrototype()
      .setOnResponseHeaders { _, _, _ in
        XCTFail("Headers received instead of expected error")
      }
      .setOnResponseData { _, _, _ in
        XCTFail("Data received instead of expected error")
      }
      // The unmatched expectation will cause a local reply which gets translated in Envoy Mobile to
      // an error.
      .setOnError { error, _ in
         XCTAssertEqual(error.errorCode, 2) // 503/Connection Failure
         callbackReceivedError.fulfill()
      }
      .setOnCancel { _ in
        XCTFail("Unexpected call to onCancel response callback")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    engine.terminate()
  }
}
