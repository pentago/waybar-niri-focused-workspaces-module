# AGENTS.md

Rust `cdylib` loaded via Waybar's CFFI ABI. Shows niri workspaces of the **focused output** on every bar (not the bar's own output). Single purpose, small codebase: `src/lib.rs` (GTK widget logic) + `src/ipc.rs` (niri IPC over Unix socket).

## Build

```bash
make          # cargo build --release + man page
make check    # cargo test
```

Requires: `libgtk-3-dev` (Debian) or `gtk3` (Arch), `scdoc` for man page, `NIRI_SOCKET` env var for runtime (niri must be running).

Output: `target/release/libniri_focused_workspaces.so`

## Tests

Unit tests only, inline in `src/lib.rs` (`#[cfg(test)] mod tests`). Run with `cargo test`. No integration tests, no CI test step beyond release workflow.

## Architecture

- `wbcffi_init` / `wbcffi_update` / `wbcffi_deinit` are the CFFI entry points called by Waybar.
- Background thread subscribes to niri's `EventStream` via Unix socket; workspace events trigger a re-fetch.
- All GTK calls are raw FFI (`*-sys` crates, no safe wrappers). All unsafe blocks are deliberate — this is a C ABI boundary.
- `wbcffi_version` (static, value 2) must match Waybar's expected ABI version.

## Conventions

- No `rustfmt.toml`, no clippy config, no linter — bare Rust project.
- Format strings in `label_text` substitute `{icon}`, `{value}`, `{name}`, `{index}`, `{output}` literally (no format specs).
- Logging: `warn()` macro prints to stderr with `niri-focused-workspaces:` prefix.
