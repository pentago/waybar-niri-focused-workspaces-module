#include "niri_ipc.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace niri {
namespace {

int connectSocket() {
  const char* path = std::getenv("NIRI_SOCKET");
  if (path == nullptr || *path == '\0') {
    throw std::runtime_error{"NIRI_SOCKET is not set, is niri running?"};
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (std::strlen(path) >= sizeof(address.sun_path)) {
    throw std::runtime_error{std::string{"NIRI_SOCKET path is too long: "} + path};
  }
  std::strcpy(address.sun_path, path);

  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error{std::string{"socket(): "} + std::strerror(errno)};
  }
  if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    const std::string reason = std::strerror(errno);
    close(fd);
    throw std::runtime_error{std::string{"connect("} + path + "): " + reason};
  }
  return fd;
}

void writeAll(int fd, const std::string& data) {
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t chunk = write(fd, data.data() + written, data.size() - written);
    if (chunk <= 0) {
      throw std::runtime_error{std::string{"write(): "} + std::strerror(errno)};
    }
    written += static_cast<std::size_t>(chunk);
  }
}

Json::Value parse(const char* text, std::size_t length) {
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  Json::Value value;
  std::string errors;
  if (!reader->parse(text, text + length, &value, &errors)) {
    throw std::runtime_error{"Malformed niri IPC response: " + errors};
  }
  return value;
}

}  // namespace

EventStream::EventStream(Handler on_event) : on_event_(std::move(on_event)) {
  const int fd = connectSocket();
  try {
    writeAll(fd, "\"EventStream\"\n");
  } catch (...) {
    close(fd);
    throw;
  }

  channel_ = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref(channel_, TRUE);
  // Without this the read loop blocks the GTK main loop once it drains the
  // buffered events, and the whole bar stops painting.
  g_io_channel_set_flags(channel_, G_IO_FLAG_NONBLOCK, nullptr);
  watch_ = g_io_add_watch(channel_, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
                          &EventStream::onReadable, this);
}

EventStream::~EventStream() {
  if (watch_ != 0) {
    g_source_remove(watch_);
  }
  if (channel_ != nullptr) {
    g_io_channel_unref(channel_);
  }
}

gboolean EventStream::onReadable(GIOChannel* channel, GIOCondition condition, gpointer data) {
  auto* self = static_cast<EventStream*>(data);

  if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
    g_warning("niri-focused-workspaces: niri closed the event stream");
    self->watch_ = 0;
    return G_SOURCE_REMOVE;
  }

  for (;;) {
    gchar* line = nullptr;
    gsize length = 0;
    GError* error = nullptr;
    const GIOStatus status = g_io_channel_read_line(channel, &line, &length, nullptr, &error);

    if (status == G_IO_STATUS_AGAIN) {
      return G_SOURCE_CONTINUE;
    }
    if (status != G_IO_STATUS_NORMAL) {
      if (error != nullptr) {
        g_warning("niri-focused-workspaces: read failed: %s", error->message);
        g_error_free(error);
      }
      g_free(line);
      self->watch_ = 0;
      return G_SOURCE_REMOVE;
    }

    try {
      self->on_event_(parse(line, length));
    } catch (const std::exception& e) {
      g_warning("niri-focused-workspaces: %s", e.what());
    }
    g_free(line);
  }
}

void focusWorkspace(std::uint64_t id) {
  Json::Value request{Json::objectValue};
  request["Action"]["FocusWorkspace"]["reference"]["Id"] = static_cast<Json::UInt64>(id);

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";

  const int fd = connectSocket();
  try {
    writeAll(fd, Json::writeString(builder, request) + "\n");
    // niri answers with a single result line; drain it so the request is not
    // discarded when we close the socket.
    char discard[512];
    read(fd, discard, sizeof(discard));
  } catch (...) {
    close(fd);
    throw;
  }
  close(fd);
}

}  // namespace niri
