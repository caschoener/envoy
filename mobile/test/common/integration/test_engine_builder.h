#pragma once

#include "library/cc/engine_builder.h"

namespace Envoy {
namespace Platform {

// An extension to the EngineBuilder class to be used only for tests.
//
// Most tests should use the EngineBuilder to edit the engine config with builder APIs.
// This class adds additional functionality to build from a fully custom yaml.
class TestEngineBuilder : EngineBuilder {
  using EngineBuilder::EngineBuilder;

public:
  // Creates an Envoy Engine from the provided config string and optionally waits
  // until the engine is running before returning the Engine as a shared_ptr.
  EngineSharedPtr buildWithString(std::string config, bool block_until_running = false);

private:
};

} // namespace Platform
} // namespace Envoy
