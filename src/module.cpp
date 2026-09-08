#include <fmt/args.h>
#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "niri_ipc.hpp"
#include "waybar_cffi_module.h"

namespace {

void setClass(GtkWidget* widget, const char* name, bool enabled) {
  GtkStyleContext* context = gtk_widget_get_style_context(widget);
  if (enabled) {
    gtk_style_context_add_class(context, name);
  } else {
    gtk_style_context_remove_class(context, name);
  }
}

Json::Value parseConfig(const wbcffi_config_entry* entries, std::size_t length) {
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};

  Json::Value config{Json::objectValue};
  for (std::size_t i = 0; i < length; i++) {
    const char* raw = entries[i].value;
    Json::Value value;
    std::string errors;
    // ABI 2 hands every value over as JSON; ABI 1 passes strings bare.
    if (reader->parse(raw, raw + std::strlen(raw), &value, &errors)) {
      config[entries[i].key] = value;
    } else {
      config[entries[i].key] = raw;
    }
  }
  return config;
}

class Module {
 public:
  Module(const wbcffi_init_info& info, Json::Value config)
      : waybar_(info.obj), queue_update_(info.queue_update), config_(std::move(config)) {
    box_ = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(GTK_WIDGET(box_), "workspaces");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(box_)), "module");
    gtk_container_add(GTK_CONTAINER(info.get_root_widget(info.obj)), GTK_WIDGET(box_));

    stream_ = std::make_unique<niri::EventStream>([this](const Json::Value& e) { onEvent(e); });
  }

  void update() {
    syncOrientation();
    const std::string output = focusedOutput();

    for (auto it = buttons_.begin(); it != buttons_.end();) {
      const auto workspace = workspaces_.find(it->first);
      if (workspace == workspaces_.end() || workspace->second["output"].asString() != output) {
        gtk_widget_destroy(GTK_WIDGET(it->second));
        it = buttons_.erase(it);
      } else {
        ++it;
      }
    }

    for (const auto& [id, workspace] : workspaces_) {
      if (workspace["output"].asString() != output) {
        continue;
      }
      GtkButton* button = syncButton(id, workspace);
      gtk_box_reorder_child(box_, GTK_WIDGET(button), workspace["idx"].asInt() - 1);
    }
    gtk_widget_show_all(GTK_WIDGET(box_));
  }

 private:
  void onEvent(const Json::Value& event) {
    if (event.isMember("WorkspacesChanged")) {
      workspaces_.clear();
      for (const auto& workspace : event["WorkspacesChanged"]["workspaces"]) {
        workspaces_[workspace["id"].asUInt64()] = workspace;
      }
    } else if (event.isMember("WorkspaceActivated")) {
      activate(event["WorkspaceActivated"]);
    } else if (event.isMember("WorkspaceUrgencyChanged")) {
      const auto& data = event["WorkspaceUrgencyChanged"];
      if (auto it = workspaces_.find(data["id"].asUInt64()); it != workspaces_.end()) {
        it->second["is_urgent"] = data["urgent"];
      }
    } else if (event.isMember("WorkspaceActiveWindowChanged")) {
      const auto& data = event["WorkspaceActiveWindowChanged"];
      if (auto it = workspaces_.find(data["workspace_id"].asUInt64()); it != workspaces_.end()) {
        it->second["active_window_id"] = data["active_window_id"];
      }
    } else {
      return;
    }
    queue_update_(waybar_);
  }

  void activate(const Json::Value& data) {
    const auto activated = workspaces_.find(data["id"].asUInt64());
    if (activated == workspaces_.end()) {
      return;
    }
    const std::string output = activated->second["output"].asString();
    const bool focused = data["focused"].asBool();

    for (auto& [id, workspace] : workspaces_) {
      if (workspace["output"].asString() == output) {
        workspace["is_active"] = false;
      }
      if (focused) {
        workspace["is_focused"] = false;
      }
    }
    activated->second["is_active"] = true;
    activated->second["is_focused"] = focused;
  }

  /// The output holding focus, falling back to the last known one so the bar
  /// never blanks while niri reports a transient focus-less state.
  std::string focusedOutput() {
    for (const auto& [id, workspace] : workspaces_) {
      if (workspace["is_focused"].asBool()) {
        focused_output_ = workspace["output"].asString();
        break;
      }
    }
    return focused_output_;
  }

  GtkButton* syncButton(std::uint64_t id, const Json::Value& workspace) {
    auto it = buttons_.find(id);
    if (it == buttons_.end()) {
      it = buttons_.emplace(id, create(id)).first;
    }
    GtkWidget* widget = GTK_WIDGET(it->second);

    setClass(widget, "focused", workspace["is_focused"].asBool());
    setClass(widget, "active", workspace["is_active"].asBool());
    setClass(widget, "urgent", workspace["is_urgent"].asBool());
    setClass(widget, "empty", workspace["active_window_id"].isNull());

    const std::string value = name(workspace);
    gtk_widget_set_name(widget, ("niri-workspace-" + value).c_str());

    GtkLabel* label = GTK_LABEL(gtk_bin_get_child(GTK_BIN(widget)));
    const std::string text = format(value, workspace);
    if (config_["disable-markup"].asBool()) {
      gtk_label_set_text(label, text.c_str());
    } else {
      gtk_label_set_markup(label, text.c_str());
    }
    return it->second;
  }

  GtkButton* create(std::uint64_t id) {
    GtkButton* button = GTK_BUTTON(gtk_button_new_with_label(""));
    gtk_button_set_relief(button, GTK_RELIEF_NONE);
    gtk_box_pack_start(box_, GTK_WIDGET(button), FALSE, FALSE, 0);

    if (!config_["disable-click"].asBool()) {
      g_signal_connect(button, "pressed", G_CALLBACK(&Module::onPressed),
                       reinterpret_cast<gpointer>(static_cast<guintptr>(id)));
    }
    return button;
  }

  static void onPressed(GtkButton*, gpointer data) {
    try {
      niri::focusWorkspace(static_cast<std::uint64_t>(reinterpret_cast<guintptr>(data)));
    } catch (const std::exception& e) {
      g_warning("niri-focused-workspaces: cannot switch workspace: %s", e.what());
    }
  }

  static std::string name(const Json::Value& workspace) {
    if (!workspace["name"].isNull()) {
      return workspace["name"].asString();
    }
    return std::to_string(workspace["idx"].asUInt());
  }

  std::string format(const std::string& value, const Json::Value& workspace) const {
    if (!config_["format"].isString()) {
      return value;
    }
    try {
      return fmt::format(fmt::runtime(config_["format"].asString()),
                         fmt::arg("icon", icon(value, workspace)), fmt::arg("value", value),
                         fmt::arg("name", workspace["name"].asString()),
                         fmt::arg("index", workspace["idx"].asUInt()),
                         fmt::arg("output", workspace["output"].asString()));
    } catch (const std::exception& e) {
      g_warning("niri-focused-workspaces: bad format: %s", e.what());
      return value;
    }
  }

  std::string icon(const std::string& value, const Json::Value& workspace) const {
    const Json::Value& icons = config_["format-icons"];
    if (!icons.isObject()) {
      return value;
    }
    const auto pick = [&](const char* key) { return icons.isMember(key); };

    if (workspace["is_urgent"].asBool() && pick("urgent")) return icons["urgent"].asString();
    if (workspace["active_window_id"].isNull() && pick("empty")) return icons["empty"].asString();
    if (workspace["is_focused"].asBool() && pick("focused")) return icons["focused"].asString();
    if (workspace["is_active"].asBool() && pick("active")) return icons["active"].asString();
    if (!workspace["name"].isNull() && pick(workspace["name"].asCString())) {
      return icons[workspace["name"].asString()].asString();
    }
    const std::string index = std::to_string(workspace["idx"].asUInt());
    if (pick(index.c_str())) return icons[index].asString();
    if (pick("default")) return icons["default"].asString();
    return value;
  }

  /// Waybar packs modules into an orientable box whose orientation follows the
  /// bar's, but the CFFI ABI never tells us which one it is.
  void syncOrientation() {
    GtkWidget* root = gtk_widget_get_parent(GTK_WIDGET(box_));
    GtkWidget* parent = root != nullptr ? gtk_widget_get_parent(root) : nullptr;
    if (parent != nullptr && GTK_IS_ORIENTABLE(parent)) {
      gtk_orientable_set_orientation(GTK_ORIENTABLE(box_),
                                     gtk_orientable_get_orientation(GTK_ORIENTABLE(parent)));
    }
  }

  wbcffi_module* waybar_;
  void (*queue_update_)(wbcffi_module*);
  Json::Value config_;
  GtkBox* box_{nullptr};
  std::string focused_output_;
  std::map<std::uint64_t, Json::Value> workspaces_;
  std::map<std::uint64_t, GtkButton*> buttons_;
  std::unique_ptr<niri::EventStream> stream_;
};

}  // namespace

extern "C" {

const size_t wbcffi_version = 2;

void* wbcffi_init(const wbcffi_init_info* init_info, const wbcffi_config_entry* config_entries,
                  size_t config_entries_len) {
  try {
    return new Module{*init_info, parseConfig(config_entries, config_entries_len)};
  } catch (const std::exception& e) {
    g_critical("niri-focused-workspaces: %s", e.what());
    return nullptr;
  }
}

void wbcffi_deinit(void* instance) { delete static_cast<Module*>(instance); }

void wbcffi_update(void* instance) {
  try {
    static_cast<Module*>(instance)->update();
  } catch (const std::exception& e) {
    g_warning("niri-focused-workspaces: %s", e.what());
  }
}
}
