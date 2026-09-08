#pragma once

#include <json/json.h>

#include <cstdint>
#include <map>
#include <string>

namespace niri {

/// Mirror of niri's workspace list, folded from the IPC event stream.
///
/// Pure state: no GTK, no sockets, so it can be exercised in isolation.
class Workspaces {
 public:
  using Map = std::map<std::uint64_t, Json::Value>;

  /// Folds one IPC event into the state. Returns false for events that carry
  /// no workspace information.
  bool apply(const Json::Value& event);

  const Map& all() const { return workspaces_; }

  /// The output holding focus, falling back to the last known one so callers
  /// never see an empty result while niri reports a transient focus-less state.
  const std::string& focusedOutput();

 private:
  void activate(const Json::Value& data);

  Map workspaces_;
  std::string focused_output_;
};

}  // namespace niri
