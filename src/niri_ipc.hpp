#pragma once

#include <glib.h>
#include <json/json.h>

#include <cstdint>
#include <functional>

namespace niri {

/// Streams niri IPC events into the GLib main loop.
///
/// Connects to $NIRI_SOCKET, subscribes to the event stream and invokes
/// `on_event` for every decoded event. Throws std::runtime_error if the socket
/// is missing or unreachable.
class EventStream {
 public:
  using Handler = std::function<void(const Json::Value&)>;

  explicit EventStream(Handler on_event);
  ~EventStream();

  EventStream(const EventStream&) = delete;
  EventStream& operator=(const EventStream&) = delete;

 private:
  static gboolean onReadable(GIOChannel* channel, GIOCondition condition, gpointer data);

  Handler on_event_;
  GIOChannel* channel_{nullptr};
  guint watch_{0};
};

/// Asks niri to focus the workspace with the given id.
void focusWorkspace(std::uint64_t id);

}  // namespace niri
