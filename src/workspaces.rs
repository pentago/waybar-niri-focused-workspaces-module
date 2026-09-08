use std::collections::BTreeMap;

use serde_json::Value;

/// Mirror of niri's workspace list, folded from the IPC event stream.
///
/// Pure state: no GTK, no sockets, so it can be exercised in isolation.
#[derive(Default)]
pub struct Workspaces {
    all: BTreeMap<u64, Value>,
    focused_output: String,
}

impl Workspaces {
    /// Folds one IPC event into the state. Returns false for events that carry
    /// no workspace information.
    pub fn apply(&mut self, event: &Value) -> bool {
        if let Some(data) = event.get("WorkspacesChanged") {
            self.all = data["workspaces"]
                .as_array()
                .map(|list| list.iter().map(|ws| (id(ws), ws.clone())).collect())
                .unwrap_or_default();
        } else if let Some(data) = event.get("WorkspaceActivated") {
            self.activate(data);
        } else if let Some(data) = event.get("WorkspaceUrgencyChanged") {
            if let Some(ws) = self.all.get_mut(&id(data)) {
                ws["is_urgent"] = data["urgent"].clone();
            }
        } else if let Some(data) = event.get("WorkspaceActiveWindowChanged") {
            if let Some(ws) = self.all.get_mut(&data["workspace_id"].as_u64().unwrap_or(0)) {
                ws["active_window_id"] = data["active_window_id"].clone();
            }
        } else {
            return false;
        }
        true
    }

    pub fn all(&self) -> &BTreeMap<u64, Value> {
        &self.all
    }

    /// The output holding focus, falling back to the last known one so callers
    /// never see an empty result while niri reports a transient focus-less state.
    pub fn focused_output(&mut self) -> &str {
        if let Some(ws) = self.all.values().find(|ws| ws["is_focused"] == true) {
            self.focused_output = ws["output"].as_str().unwrap_or_default().to_owned();
        }
        &self.focused_output
    }

    fn activate(&mut self, data: &Value) {
        let Some(activated) = self.all.get(&id(data)) else {
            return;
        };
        let output = activated["output"].clone();
        let focused = data["focused"] == true;

        for ws in self.all.values_mut() {
            if ws["output"] == output {
                ws["is_active"] = false.into();
            }
            if focused {
                ws["is_focused"] = false.into();
            }
        }
        let activated = self.all.get_mut(&id(data)).expect("checked above");
        activated["is_active"] = true.into();
        activated["is_focused"] = focused.into();
    }
}

fn id(value: &Value) -> u64 {
    value["id"].as_u64().unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn snapshot() -> Value {
        json!({"WorkspacesChanged": {"workspaces": [
            {"id": 1, "idx": 1, "name": "comms", "output": "DP-2", "is_urgent": false,
             "is_active": true, "is_focused": false, "active_window_id": 18},
            {"id": 2, "idx": 1, "name": "dev", "output": "HDMI-A-1", "is_urgent": false,
             "is_active": true, "is_focused": true, "active_window_id": 15},
            {"id": 3, "idx": 2, "name": null, "output": "HDMI-A-1", "is_urgent": false,
             "is_active": false, "is_focused": false, "active_window_id": null}]}})
    }

    fn seeded() -> Workspaces {
        let mut state = Workspaces::default();
        assert!(state.apply(&snapshot()));
        state
    }

    #[test]
    fn snapshot_seeds_every_output() {
        let mut state = seeded();
        assert_eq!(state.all().len(), 3);
        assert_eq!(state.focused_output(), "HDMI-A-1");
    }

    #[test]
    fn activation_moves_focus_across_outputs() {
        let mut state = seeded();
        assert!(state.apply(&json!({"WorkspaceActivated": {"id": 1, "focused": true}})));

        assert_eq!(state.focused_output(), "DP-2");
        assert_eq!(state.all()[&1]["is_focused"], true);
        assert_eq!(state.all()[&2]["is_focused"], false);
        assert_eq!(
            state.all()[&2]["is_active"],
            true,
            "other outputs keep their active workspace"
        );
    }

    #[test]
    fn activation_without_focus_only_changes_its_own_output() {
        let mut state = seeded();
        assert!(state.apply(&json!({"WorkspaceActivated": {"id": 3, "focused": false}})));

        assert_eq!(state.all()[&3]["is_active"], true);
        assert_eq!(state.all()[&2]["is_active"], false);
        assert_eq!(state.all()[&1]["is_active"], true);
        assert_eq!(
            state.focused_output(),
            "HDMI-A-1",
            "focus is unchanged, so the output is too"
        );
    }

    #[test]
    fn emptiness_follows_the_active_window() {
        let mut state = seeded();
        let event = |window| json!({"WorkspaceActiveWindowChanged":
            {"workspace_id": 3, "active_window_id": window}});

        state.apply(&event(json!(42)));
        assert_eq!(state.all()[&3]["active_window_id"], 42);

        state.apply(&event(Value::Null));
        assert!(state.all()[&3]["active_window_id"].is_null());
    }

    #[test]
    fn urgency_is_tracked() {
        let mut state = seeded();
        state.apply(&json!({"WorkspaceUrgencyChanged": {"id": 1, "urgent": true}}));
        assert_eq!(state.all()[&1]["is_urgent"], true);
    }

    #[test]
    fn unrelated_events_are_ignored() {
        let mut state = seeded();
        assert!(!state.apply(&json!({"WindowsChanged": {"windows": []}})));
        assert!(!state.apply(&json!({"OverviewOpenedOrClosed": {"is_open": true}})));
    }

    #[test]
    fn focused_output_survives_an_empty_state() {
        let mut state = seeded();
        assert_eq!(state.focused_output(), "HDMI-A-1");

        state.apply(&json!({"WorkspacesChanged": {"workspaces": []}}));
        assert_eq!(
            state.focused_output(),
            "HDMI-A-1",
            "last known output is kept, the bar never blanks"
        );
    }
}
