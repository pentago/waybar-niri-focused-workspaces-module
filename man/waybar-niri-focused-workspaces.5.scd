waybar-niri-focused-workspaces(5)

# NAME

waybar - niri focused-workspaces CFFI module

# DESCRIPTION

A Waybar CFFI module that shows the niri workspaces of the output that
currently holds focus, on every bar, regardless of which output the bar itself
lives on.

The built-in *niri/workspaces* module always shows the workspaces of the bar's
own output. This module is the equivalent of DankMaterialShell's
_workspaceFollowFocus_ setting: with a single bar on one monitor, the workspace
row follows you across monitors.

It renders real GTK buttons under the same widget name and CSS classes as
*niri/workspaces*, so existing *#workspaces* styling applies unchanged.

# CONFIGURATION

Addressed by *cffi/<name>*

*module_path* ++
	typeof: string ++
	Path to the shared library, normally
	_/usr/local/bin/niri-focused-workspaces.so_ for a local install, or
	_/usr/bin/niri-focused-workspaces.so_ when installed from a package.

*format* ++
	typeof: string ++
	default: the workspace name, or its index when unnamed ++
	How each workspace should be displayed. Placeholders are substituted
	literally; unlike *niri/workspaces* there is no format spec support.

*format-icons* ++
	typeof: object ++
	Icons selected by workspace name, index or state. See *ICONS*.

*disable-click* ++
	typeof: bool ++
	default: false ++
	If set to true, clicking a workspace no longer switches to it.

*disable-markup* ++
	typeof: bool ++
	default: false ++
	If set to true, button labels escape Pango markup.

# FORMAT REPLACEMENTS

*{value}*: Name of the workspace, or index for unnamed workspaces.

*{name}*: Name of the workspace, empty for unnamed workspaces.

*{icon}*: Icon, as defined in *format-icons*.

*{index}*: Index of the workspace on its output.

*{output}*: Output the workspace is located on.

# ICONS

In addition to workspace name and index matching, these keys are recognised, in
descending priority: *urgent*, *empty*, *focused*, *active*, *default*.

# STYLE

- *#workspaces* the module box
- *#workspaces button* a workspace button
- *#workspaces button.focused* the workspace with keyboard focus
- *#workspaces button.active* the active workspace of its output
- *#workspaces button.urgent* a workspace with urgent windows
- *#workspaces button.empty* a workspace without windows

Unlike *niri/workspaces* there is no *.current_output* class: the CFFI ABI does
not tell a module which output its bar is on.

# EXAMPLES

```
"modules-left": ["cffi/workspaces"],
"cffi/workspaces": {
	"module_path": "/usr/local/bin/niri-focused-workspaces.so",
	"format": "{index}"
}
```

# SEE ALSO

*waybar*(1), *waybar-niri-workspaces*(5), *waybar-cffi*(5)
