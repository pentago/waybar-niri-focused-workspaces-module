#include "workspaces.hpp"

namespace niri {

bool Workspaces::apply(const Json::Value& event) {
  if (event.isMember("WorkspacesChanged")) {
    workspaces_.clear();
    for (const auto& workspace : event["WorkspacesChanged"]["workspaces"]) {
      workspaces_[workspace["id"].asUInt64()] = workspace;
    }
    return true;
  }
  if (event.isMember("WorkspaceActivated")) {
    activate(event["WorkspaceActivated"]);
    return true;
  }
  if (event.isMember("WorkspaceUrgencyChanged")) {
    const auto& data = event["WorkspaceUrgencyChanged"];
    if (auto it = workspaces_.find(data["id"].asUInt64()); it != workspaces_.end()) {
      it->second["is_urgent"] = data["urgent"];
    }
    return true;
  }
  if (event.isMember("WorkspaceActiveWindowChanged")) {
    const auto& data = event["WorkspaceActiveWindowChanged"];
    if (auto it = workspaces_.find(data["workspace_id"].asUInt64()); it != workspaces_.end()) {
      it->second["active_window_id"] = data["active_window_id"];
    }
    return true;
  }
  return false;
}

void Workspaces::activate(const Json::Value& data) {
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

const std::string& Workspaces::focusedOutput() {
  for (const auto& [id, workspace] : workspaces_) {
    if (workspace["is_focused"].asBool()) {
      focused_output_ = workspace["output"].asString();
      break;
    }
  }
  return focused_output_;
}

}  // namespace niri
