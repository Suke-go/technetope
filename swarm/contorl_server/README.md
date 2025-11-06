# toio Control Server

This folder contains the C++ control server, its WebSocket/JSON docs, and a build-less WebUI (`webui/`) for operators.

## WebUI quick start
1. Build or launch the control server so that it exposes a WebSocket endpoint at `/ws/ui` (see `docs/control_webui_ws_spec.md`).
2. Open `webui/index.html` directly in a modern browser (double-clicking the file or using `file://` is fine; no bundler is required).
3. Enter the server URL if it differs from the default `ws://localhost:8080/ws/ui`, optionally tick **Use mock data** for offline testing, and press **Connect**.
4. After the connection handshake, use the Relay/Cube panes for status, the canvas for a quick spatial overview, and the forms inside the Commands drawer to send `manual_drive`, `set_goal`, `set_led`, and `set_group` messages.
5. Use the **Snapshot** button at the top to force a `request_snapshot`, the cube table to select targets (leave the Targets input empty to reuse the current selection), and the log panel to review acknowledgements or server-side warnings.

## Field configuration
- `config/control_server.json` accepts a `field` block with `top_left` / `bottom_right` points (millimetres) to describe the playable area. Omit the block to fall back to the default `(45,45)` → `(455,455)`.
- The control server loads these values at startup, keeps them in `ControlServerConfig`, and broadcasts them to WebUI clients through a dedicated `field_info` message plus the `snapshot.field` payload so the canvas can auto-scale.

### Mock data mode
- Checking **Use mock data** spins up an in-browser `MockSocket` that mimics the real server: snapshots are generated immediately, cube poses drift slowly, and random relay/log/fleet updates are streamed so you can design the UI and verify JSON payloads before the C++ side is ready.
- Mock mode understands the same commands as the actual server (`manual_drive`, `set_goal`, `set_led`, `set_group`, `request_snapshot`) and emits realistic `ack` responses so that the UI flow (forms → toast → log) can be exercised end-to-end.

### Development notes
- The SPA is implemented with plain HTML/CSS/ES Modules. `webui/main.js` drives state via a lightweight `EventTarget` store and draws the field using `<canvas>` per `DESIGN.md`.
- No external dependencies are required; edit the files under `webui/` and refresh the browser to iterate.
- Key specs live in `docs/control_webui_ws_spec.md`. Update them alongside UI changes whenever payloads or message types evolve.
