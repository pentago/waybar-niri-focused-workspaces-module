# waybar-niri-focused-workspaces

A [Waybar](https://github.com/Alexays/Waybar) CFFI module that shows the
[niri](https://github.com/YaLTeR/niri) workspaces of the **focused output** —
on every bar, regardless of which output the bar itself lives on.

Waybar's built-in `niri/workspaces` module always shows the workspaces of the
bar's own output; the filter is hardcoded to `bar_.output->name` and there is no
option to change it. If you run a single bar on your laptop screen while working
across external monitors, that bar is stuck showing the laptop's workspaces.

This module is the equivalent of DankMaterialShell's `workspaceFollowFocus`
setting: the workspace row follows your focus across monitors.

It is a plain Rust `cdylib` loaded through Waybar's
[CFFI ABI](https://github.com/Alexays/Waybar/wiki/Module:-CFFI) — **no patched
Waybar required**, it works with the stock `waybar` package. Dependencies are
`serde_json` plus the raw `*-sys` GTK3 bindings; no safe-wrapper crates.

## Styling

The module renders real `GtkButton`s inside a box named `workspaces`, carrying
the same CSS classes as `niri/workspaces`. Existing stylesheets apply unchanged:

```css
#workspaces button { padding-bottom: 2px; }
#workspaces button.empty { opacity: 0.5; }
#workspaces button.focused { font-weight: bold; border-bottom: 2px solid @accent; }
```

Classes: `focused`, `active`, `urgent`, `empty`. Each button is also named
`niri-workspace-<name>`.

There is no `.current_output` class — the CFFI ABI does not expose which output
a module's bar is on.

## Install

From the AUR:

```bash
paru -S waybar-niri-focused-workspaces
```

From source (installs to `/usr/local`, override with `PREFIX=`):

```bash
make
sudo make install     # /usr/local/bin/niri-focused-workspaces
sudo make uninstall
```

## Configure

```jsonc
{
  "modules-left": ["cffi/workspaces"],
  "cffi/workspaces": {
    "module_path": "/usr/local/bin/niri-focused-workspaces",
    "format": "{index}"
  }
}
```

The AUR package installs to `/usr/bin/niri-focused-workspaces` instead —
pacman packages may not write to `/usr/local`.

Options: `format`, `format-icons`, `disable-click`, `disable-markup` — same
semantics as `niri/workspaces`, except that `format` substitutes placeholders
literally and does not support format specs. See
`man 5 waybar-niri-focused-workspaces`.

`all-outputs` and `current-only` are deliberately absent: both are meaningless
once the module always tracks a single, focused output.

## Releasing

`aur/PKGBUILD` builds from a GitHub release tarball. To cut a release:

1. Bump `version` in `Cargo.toml` and `pkgver` in `aur/PKGBUILD`.
2. Tag `vX.Y.Z` and push; GitHub serves the tarball.
3. Replace `sha256sums=('SKIP')` with the real sum (`updpkgsums` in `aur/`).
4. Regenerate `aur/.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO`).
5. Push `PKGBUILD` and `.SRCINFO` to
   `ssh://aur@aur.archlinux.org/waybar-niri-focused-workspaces.git`.

## License

MIT.
