#include <string>

#include "absl/synchronization/notification.h"
#include "test_engine_builder.h"
#include "library/common/main_interface.h"
#include "library/common/engine.h"

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
    auto options = std::make_unique<Envoy::OptionsImpl>();
    options->setConfigYaml(std::move(config));
    options->setLogLevel(options->parseAndValidateLogLevel(logLevelToString(log_level_).c_str()));
    options->setConcurrency(1);
    cast_engine->run(std::move(options));
  }
  if (block_until_running) {
    engine_running.WaitForNotification();
  }
  auto engine_ptr = EngineSharedPtr(engine);

  return engine;
}

} // namespace Platform
} // namespace Envoy
