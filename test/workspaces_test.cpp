#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "workspaces.hpp"

namespace {

Json::Value json(const std::string& text) {
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  Json::Value value;
  std::string errors;
  assert(reader->parse(text.data(), text.data() + text.size(), &value, &errors));
  return value;
}

Json::Value snapshot() {
  return json(R"({"WorkspacesChanged":{"workspaces":[
    {"id":1,"idx":1,"name":"comms","output":"DP-2","is_urgent":false,"is_active":true,
     "is_focused":false,"active_window_id":18},
    {"id":2,"idx":1,"name":"dev","output":"HDMI-A-1","is_urgent":false,"is_active":true,
     "is_focused":true,"active_window_id":15},
    {"id":3,"idx":2,"name":null,"output":"HDMI-A-1","is_urgent":false,"is_active":false,
     "is_focused":false,"active_window_id":null}]}})");
}

void snapshotSeedsEveryOutput() {
  niri::Workspaces state;
  assert(state.apply(snapshot()));
  assert(state.all().size() == 3);
  assert(state.focusedOutput() == "HDMI-A-1");
}

void activationMovesFocusAcrossOutputs() {
  niri::Workspaces state;
  state.apply(snapshot());

  assert(state.apply(json(R"({"WorkspaceActivated":{"id":1,"focused":true}})")));
  assert(state.focusedOutput() == "DP-2");
  assert(state.all().at(1)["is_focused"].asBool());
  assert(!state.all().at(2)["is_focused"].asBool());
  assert(state.all().at(2)["is_active"].asBool() && "other outputs keep their active workspace");
}

void activationWithoutFocusOnlyChangesItsOwnOutput() {
  niri::Workspaces state;
  state.apply(snapshot());

  assert(state.apply(json(R"({"WorkspaceActivated":{"id":3,"focused":false}})")));
  assert(state.all().at(3)["is_active"].asBool());
  assert(!state.all().at(2)["is_active"].asBool());
  assert(state.all().at(1)["is_active"].asBool());
  assert(state.focusedOutput() == "HDMI-A-1" && "focus is unchanged, so the output is too");
}

void emptinessFollowsTheActiveWindow() {
  niri::Workspaces state;
  state.apply(snapshot());

  state.apply(json(R"({"WorkspaceActiveWindowChanged":{"workspace_id":3,"active_window_id":42}})"));
  assert(!state.all().at(3)["active_window_id"].isNull());

  state.apply(json(R"({"WorkspaceActiveWindowChanged":{"workspace_id":3,"active_window_id":null}})"));
  assert(state.all().at(3)["active_window_id"].isNull());
}

void urgencyIsTracked() {
  niri::Workspaces state;
  state.apply(snapshot());
  state.apply(json(R"({"WorkspaceUrgencyChanged":{"id":1,"urgent":true}})"));
  assert(state.all().at(1)["is_urgent"].asBool());
}

void unrelatedEventsAreIgnored() {
  niri::Workspaces state;
  state.apply(snapshot());
  assert(!state.apply(json(R"({"WindowsChanged":{"windows":[]}})")));
  assert(!state.apply(json(R"({"OverviewOpenedOrClosed":{"is_open":true}})")));
}

void focusedOutputSurvivesAnEmptyState() {
  niri::Workspaces state;
  state.apply(snapshot());
  assert(state.focusedOutput() == "HDMI-A-1");

  state.apply(json(R"({"WorkspacesChanged":{"workspaces":[]}})"));
  assert(state.focusedOutput() == "HDMI-A-1" && "last known output is kept, the bar never blanks");
}

}  // namespace

int main() {
  snapshotSeedsEveryOutput();
  activationMovesFocusAcrossOutputs();
  activationWithoutFocusOnlyChangesItsOwnOutput();
  emptinessFollowsTheActiveWindow();
  urgencyIsTracked();
  unrelatedEventsAreIgnored();
  focusedOutputSurvivesAnEmptyState();
  std::cout << "workspaces: all checks passed\n";
}
