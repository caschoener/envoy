#include "source/common/common/key_value_store_base.h"

#include "absl/cleanup/cleanup.h"
#include <algorithm>
#include <chrono>
#include <optional>

namespace Envoy {
namespace {

// Removes a length prefixed token from |contents| and returns the token,
// or returns absl::nullopt on failure.
absl::optional<absl::string_view> getToken(absl::string_view& contents, std::string& error) {
  const auto it = contents.find('\n');
  if (it == contents.npos) {
    error = "Bad file: no newline";
    return {};
  }
  uint64_t length;
  if (!absl::SimpleAtoi(contents.substr(0, it), &length)) {
    error = "Bad file: no length";
    return {};
  }
  contents.remove_prefix(it + 1);
  if (contents.size() < length) {
    error = "Bad file: insufficient contents";
    return {};
  }
  absl::string_view token = contents.substr(0, length);
  contents.remove_prefix(length);
  return token;
}

} // namespace

KeyValueStoreBase::KeyValueStoreBase(Event::Dispatcher& dispatcher,
                                     std::chrono::milliseconds flush_interval, uint32_t max_entries)
    : max_entries_(max_entries), flush_timer_(dispatcher.createTimer([this, flush_interval]() {
        flush();
        flush_timer_->enableTimer(flush_interval);
      })),
      ttl_manager_([this](const auto& expired) { ttlCallback(expired); }, dispatcher,
                  dispatcher.timeSource()) {
  if (flush_interval.count() > 0) {
    flush_timer_->enableTimer(flush_interval);
  }
}

void KeyValueStoreBase::ttlCallback(const std::vector<std::string>& expired) {
  for (const auto& key : expired) {
    remove(key);
  }
}

bool KeyValueStoreBase::parseContents(absl::string_view contents) {
  std::string error;
  while (!contents.empty()) {
    absl::optional<absl::string_view> key = getToken(contents, error);
    absl::optional<absl::string_view> value;
    if (key.has_value()) {
      value = getToken(contents, error);
    }
    if (!key.has_value() || !value.has_value()) {
      ENVOY_LOG(warn, error);
      return false;
    }
    addOrUpdate(key.value(), value.value());
  }
  return true;
}

void KeyValueStoreBase::addOrUpdate(absl::string_view key_view, absl::string_view value_view,
                                    absl::optional<std::chrono::milliseconds> ttl) {
  ENVOY_BUG(!under_iterate_, "addOrUpdate under the stack of iterate");
  std::string key(key_view);
  std::string value(value_view);
  // Remove ttl if <= 0
  if (ttl && ttl <= std::chrono::milliseconds(0)){
    ASSERT(false);
    ttl = absl::nullopt;
  }

  // Attempt to insert the entry into the store. If it already exists, remove
  // the old entry and insert the new one so it will be in the proper place in
  // the linked list.
  if (!store_.emplace(key, value).second) {
    store_.erase(key);
    store_.emplace(key, value);
    ttl_manager_.clear(key);
  }
  if (max_entries_ && store_.size() > max_entries_) {
    store_.pop_front();
  }
  if (ttl)
  {
    ttl_manager_.add(ttl.value(), key);
  }
  if (!flush_timer_->enabled()) {
    flush();
  }
}

void KeyValueStoreBase::remove(absl::string_view key) {
  ENVOY_BUG(!under_iterate_, "remove under the stack of iterate");
  store_.erase(std::string(key));
  if (!flush_timer_->enabled()) {
    flush();
  }
}

absl::optional<absl::string_view> KeyValueStoreBase::get(absl::string_view key) {
  auto it = store_.find(std::string(key));
  if (it == store_.end()) {
    return {};
  }
  return it->second;
}

void KeyValueStoreBase::iterate(ConstIterateCb cb) const {
  under_iterate_ = true;
  absl::Cleanup restore_under_iterate = [this] { under_iterate_ = false; };

  for (const auto& [key, value] : store_) {
    Iterate ret = cb(key, value);
    if (ret == Iterate::Break) {
      return;
    }
  }
}

} // namespace Envoy
