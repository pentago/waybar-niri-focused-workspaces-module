use std::env;
use std::io::{self, BufRead, BufReader, Write};
use std::os::unix::net::UnixStream;

use serde_json::{json, Value};

fn connect() -> io::Result<UnixStream> {
    let path = env::var("NIRI_SOCKET")
        .map_err(|_| io::Error::other("NIRI_SOCKET is not set, is niri running?"))?;
    UnixStream::connect(path)
}

/// Sends one request and returns niri's reply payload.
fn request(request: &Value) -> io::Result<Value> {
    let mut socket = connect()?;
    writeln!(socket, "{request}")?;

    let mut reply = String::new();
    BufReader::new(&socket).read_line(&mut reply)?;
    serde_json::from_str(&reply).map_err(io::Error::other)
}

/// Subscribes to niri's event stream. The returned reader yields one JSON event
/// per line; the clone is only there so the caller can shut the socket down and
/// unblock the reader.
pub fn event_stream() -> io::Result<(impl Iterator<Item = Value>, UnixStream)> {
    let mut socket = connect()?;
    socket.write_all(b"\"EventStream\"\n")?;
    let handle = socket.try_clone()?;

    let events = BufReader::new(socket)
        .lines()
        .map_while(Result::ok)
        .filter_map(|line| serde_json::from_str(&line).ok());
    Ok((events, handle))
}

pub fn workspaces() -> io::Result<Vec<Value>> {
    let reply = request(&json!("Workspaces"))?;
    Ok(reply["Ok"]["Workspaces"].as_array().cloned().unwrap_or_default())
}

pub fn focus_workspace(id: u64) -> io::Result<()> {
    request(&json!({"Action": {"FocusWorkspace": {"reference": {"Id": id}}}}))?;
    Ok(())
}
