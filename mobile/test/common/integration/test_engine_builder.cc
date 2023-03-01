#pragma once

#include <string>

#include "absl/synchronization/notification.h"
#include "test_engine_builder.h"

namespace Envoy {
namespace Platform {

EngineSharedPtr TestEngineBuilder::buildWithString(std::string config, bool block_until_running) {
  envoy_logger null_logger;
  null_logger.release = envoy_noop_const_release;
  envoy_event_tracker null_tracker{};
  absl::Notification engine_running;
  if (block_until_running) {
    setOnEngineRunning([&]() { engine_running.Notify(); });
  }
  envoy_engine_t envoy_engine =
      init_engine(callbacks_->asEnvoyEngineCallbacks(), null_logger, null_tracker);

  Engine* engine = new Engine(envoy_engine);
  if (auto cast_engine = reinterpret_cast<Envoy::Engine*>(envoy_engine)) {
    engine->run(config, log_level_, "");
  }
  if (block_until_running) {
    engine_running.WaitForNotification();
  }
  auto engine_ptr = EngineSharedPtr(engine);

  return engine;
}

} // namespace Platform
} // namespace Envoy
