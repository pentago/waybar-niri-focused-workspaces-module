mod ipc;
mod workspaces;

use std::collections::BTreeMap;
use std::ffi::{c_char, c_void, CStr, CString};
use std::mem;
use std::net::Shutdown;
use std::os::unix::net::UnixStream;
use std::ptr;
use std::sync::{Arc, Mutex};
use std::thread;

use serde_json::Value;

use workspaces::Workspaces;

#[repr(C)]
pub struct InitInfo {
    obj: *mut c_void,
    waybar_version: *const c_char,
    get_root_widget: unsafe extern "C" fn(*mut c_void) -> *mut gtk_sys::GtkContainer,
    queue_update: unsafe extern "C" fn(*mut c_void),
}

#[repr(C)]
pub struct ConfigEntry {
    key: *const c_char,
    value: *const c_char,
}

#[no_mangle]
pub static wbcffi_version: usize = 2;

/// Lets the reader thread poke Waybar's dispatcher, which is what
/// `queue_update` exists for.
struct Notifier {
    obj: *mut c_void,
    queue_update: unsafe extern "C" fn(*mut c_void),
}

unsafe impl Send for Notifier {}

impl Notifier {
    fn notify(&self) {
        unsafe { (self.queue_update)(self.obj) };
    }
}

struct Module {
    config: Value,
    container: *mut gtk_sys::GtkWidget,
    buttons: BTreeMap<u64, *mut gtk_sys::GtkWidget>,
    state: Arc<Mutex<Workspaces>>,
    socket: UnixStream,
}

/// # Safety
///
/// Called by Waybar with a valid `InitInfo` and `entries_len` config entries.
#[no_mangle]
pub unsafe extern "C" fn wbcffi_init(
    info: *const InitInfo,
    entries: *const ConfigEntry,
    entries_len: usize,
) -> *mut c_void {
    let info = &*info;

    let (events, socket) = match ipc::event_stream() {
        Ok(stream) => stream,
        Err(error) => {
            warn(format_args!("{error}"));
            return ptr::null_mut();
        }
    };

    let container = gtk_sys::gtk_box_new(gtk_sys::GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_sys::gtk_widget_set_name(container, c"workspaces".as_ptr());
    set_class(container, c"module", true);
    gtk_sys::gtk_container_add((info.get_root_widget)(info.obj), container);

    let state = Arc::new(Mutex::new(Workspaces::default()));
    let notifier = Notifier {
        obj: info.obj,
        queue_update: info.queue_update,
    };
    let shared = Arc::clone(&state);
    thread::spawn(move || {
        for event in events {
            if shared.lock().expect("state mutex").apply(&event) {
                notifier.notify();
            }
        }
    });

    Box::into_raw(Box::new(Module {
        config: parse_config(entries, entries_len),
        container,
        buttons: BTreeMap::new(),
        state,
        socket,
    }))
    .cast()
}

/// # Safety
///
/// `instance` must be a pointer returned by [`wbcffi_init`], not yet freed.
#[no_mangle]
pub unsafe extern "C" fn wbcffi_deinit(instance: *mut c_void) {
    let module = Box::from_raw(instance.cast::<Module>());
    // Unblocks the reader thread, which then falls out of its loop.
    let _ = module.socket.shutdown(Shutdown::Both);
}

/// # Safety
///
/// `instance` must be a pointer returned by [`wbcffi_init`], not yet freed.
#[no_mangle]
pub unsafe extern "C" fn wbcffi_update(instance: *mut c_void) {
    let Module {
        config,
        container,
        buttons,
        state,
        ..
    } = &mut *instance.cast::<Module>();

    sync_orientation(*container);

    let mut state = state.lock().expect("state mutex");
    let output = Value::from(state.focused_output());

    buttons.retain(|id, button| {
        let keep = state.all().get(id).is_some_and(|ws| ws["output"] == output);
        if !keep {
            gtk_sys::gtk_widget_destroy(*button);
        }
        keep
    });

    let clickable = config["disable-click"] != true;
    for (id, workspace) in state.all() {
        if workspace["output"] != output {
            continue;
        }
        let button = *buttons
            .entry(*id)
            .or_insert_with(|| create_button(*container, *id, clickable));

        set_class(button, c"focused", workspace["is_focused"] == true);
        set_class(button, c"active", workspace["is_active"] == true);
        set_class(button, c"urgent", workspace["is_urgent"] == true);
        set_class(button, c"empty", workspace["active_window_id"].is_null());

        let value = name(workspace);
        set_str(gtk_sys::gtk_widget_set_name, button, &format!("niri-workspace-{value}"));

        let label = gtk_sys::gtk_bin_get_child(button.cast()).cast::<gtk_sys::GtkLabel>();
        let setter = if config["disable-markup"] == true {
            gtk_sys::gtk_label_set_text
        } else {
            gtk_sys::gtk_label_set_markup
        };
        set_str(setter, label, &label_text(config, &value, workspace));

        let position = workspace["idx"].as_i64().unwrap_or(1) as i32 - 1;
        gtk_sys::gtk_box_reorder_child((*container).cast(), button, position);
    }
    gtk_sys::gtk_widget_show_all(*container);
}

unsafe fn create_button(container: *mut gtk_sys::GtkWidget, id: u64, clickable: bool) -> *mut gtk_sys::GtkWidget {
    let button = gtk_sys::gtk_button_new_with_label(c"".as_ptr());
    gtk_sys::gtk_button_set_relief(button.cast(), gtk_sys::GTK_RELIEF_NONE);
    gtk_sys::gtk_box_pack_start(container.cast(), button, glib_sys::GFALSE, glib_sys::GFALSE, 0);

    if clickable {
        gobject_sys::g_signal_connect_data(
            button.cast(),
            c"pressed".as_ptr(),
            Some(mem::transmute::<
                unsafe extern "C" fn(*mut gtk_sys::GtkButton, glib_sys::gpointer),
                unsafe extern "C" fn(),
            >(on_pressed)),
            id as usize as glib_sys::gpointer,
            None,
            0,
        );
    }
    button
}

unsafe extern "C" fn on_pressed(_: *mut gtk_sys::GtkButton, data: glib_sys::gpointer) {
    if let Err(error) = ipc::focus_workspace(data as usize as u64) {
        warn(format_args!("cannot switch workspace: {error}"));
    }
}

/// Waybar packs modules into an orientable box whose orientation follows the
/// bar's, but the CFFI ABI never tells us which one it is.
unsafe fn sync_orientation(container: *mut gtk_sys::GtkWidget) {
    let root = gtk_sys::gtk_widget_get_parent(container);
    if root.is_null() {
        return;
    }
    let parent = gtk_sys::gtk_widget_get_parent(root);
    if parent.is_null()
        || gobject_sys::g_type_check_instance_is_a(parent.cast(), gtk_sys::gtk_orientable_get_type())
            == glib_sys::GFALSE
    {
        return;
    }
    let orientation = gtk_sys::gtk_orientable_get_orientation(parent.cast());
    gtk_sys::gtk_orientable_set_orientation(container.cast(), orientation);
}

unsafe fn set_class(widget: *mut gtk_sys::GtkWidget, class: &CStr, enabled: bool) {
    let context = gtk_sys::gtk_widget_get_style_context(widget);
    if enabled {
        gtk_sys::gtk_style_context_add_class(context, class.as_ptr());
    } else {
        gtk_sys::gtk_style_context_remove_class(context, class.as_ptr());
    }
}

unsafe fn set_str<T>(setter: unsafe extern "C" fn(*mut T, *const c_char), target: *mut T, text: &str) {
    let text = CString::new(text).unwrap_or_default();
    setter(target, text.as_ptr());
}

unsafe fn parse_config(entries: *const ConfigEntry, entries_len: usize) -> Value {
    let mut config = serde_json::Map::new();
    for entry in std::slice::from_raw_parts(entries, entries_len) {
        let key = CStr::from_ptr(entry.key).to_string_lossy().into_owned();
        let raw = CStr::from_ptr(entry.value).to_string_lossy().into_owned();
        // ABI 2 hands every value over as JSON; ABI 1 passes strings bare.
        let value = serde_json::from_str(&raw).unwrap_or(Value::String(raw));
        config.insert(key, value);
    }
    Value::Object(config)
}

fn name(workspace: &Value) -> String {
    workspace["name"]
        .as_str()
        .map(str::to_owned)
        .unwrap_or_else(|| workspace["idx"].to_string())
}

fn label_text(config: &Value, value: &str, workspace: &Value) -> String {
    let Some(format) = config["format"].as_str() else {
        return value.to_owned();
    };
    format
        .replace("{icon}", &icon(config, value, workspace))
        .replace("{value}", value)
        .replace("{name}", workspace["name"].as_str().unwrap_or_default())
        .replace("{index}", &workspace["idx"].to_string())
        .replace("{output}", workspace["output"].as_str().unwrap_or_default())
}

fn icon(config: &Value, value: &str, workspace: &Value) -> String {
    let icons = &config["format-icons"];
    if !icons.is_object() {
        return value.to_owned();
    }
    let pick = |key: &str| icons.get(key).and_then(Value::as_str).map(str::to_owned);

    let by_state = [
        ("urgent", workspace["is_urgent"] == true),
        ("empty", workspace["active_window_id"].is_null()),
        ("focused", workspace["is_focused"] == true),
        ("active", workspace["is_active"] == true),
    ];
    for (key, active) in by_state {
        if active {
            if let Some(icon) = pick(key) {
                return icon;
            }
        }
    }
    workspace["name"]
        .as_str()
        .and_then(pick)
        .or_else(|| pick(&workspace["idx"].to_string()))
        .or_else(|| pick("default"))
        .unwrap_or_else(|| value.to_owned())
}

fn warn(message: std::fmt::Arguments) {
    eprintln!("niri-focused-workspaces: {message}");
}
