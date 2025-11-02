# Acoustics GUI Specification (Draft)

## Desired Operator Experience (Essence)
- 一覧画面で Stick が最大100台まで並び、緑＝通信良好／赤＝切断が直感的に分かる。
- どのデバイスがどの場所かを Alias で把握し、必要なときに即座に OSC コマンドを送れる。
- レイテンシ数値をざっと確認でき、異常があったときだけログで追跡できる。
- CLI で行っていた監視・送信・設定ファイルの扱いを GUI に集約し、起動後はワンクリックで運用開始できる。
- 「このデバイスはどれ？」を特定するためのテスト信号送出（短いプレーンなプリセット or テキスト通知）を備える。

## Goals
- Monitor up to ~100 StickC+2 devices in a single desktop window.
- Surface connectivity / heartbeat status clearly; raise anomalies without building a full waveform viewer.
- Allow operators to rename devices (persist aliases) and trigger OSC control/timeline dispatch to selected devices.
- Serve as the front-end for the existing C++ transport layer (`OscSender`, `OscListener`, `DeviceRegistry`).

## Core Concepts
- **DeviceRecord**: Combines `DeviceRegistry` snapshot data with GUI-local metadata.
  ```jsonc
  {
    "device_id": "dev-004b12c48530",
    "mac": "00:4B:12:C4:85:30",
    "alias": "FrontLeft",
    "fw_version": "0.1.0-dev",
    "last_seen_iso": "2025-11-02T03:55:24Z",
    "heartbeat": {
      "latency_mean_ms": 4.2,
      "latency_std_ms": 1.1,
      "state": "ok"
    }
  }
  ```
- **Alias Store**: JSON file (default `state/device_aliases.json`) managed by GUI. Device IDs map to user-defined names; updates flush immediately.
- **Timeline Preset**: Reference to a JSON timeline (same schema as scheduler). GUI selects devices and dispatches bundles via `OscSender`.

## Layout (Modeless / Docking)
- ImGui Docking を有効化し、各パネルは独立ウィンドウとしてレイアウト。ユーザーが自由に配置・常駐化できる。
- **Device Springboard ウィンドウ**
  - iPad ホーム画面風。20台ごとに列/ページを切り替え（縦スクロール + 横スクロール）。
  - Tile (200×140px 目安): Alias・ID・ステータス LED・レイテンシ・`Select`・`Test Signal` ボタン。
  - Alias クリックで inline rename。Test Signal で識別用の短いプリセットを送信。
- **Dispatch ウィンドウ**
  - 選択デバイス一覧、`Clear` ボタン。
  - Timeline 送信フォーム（ファイル選択・Lead/Base time）と `Send Timeline` ボタン。
  - 即時プリセット送信用フォーム（プリセット名・ゲイン・リードタイム）。
- **OSC Endpoint ウィンドウ**
  - Host/Port/Broadcast の設定と `Apply`。
- **Event Log ウィンドウ**
  - Heartbeat 異常・送信結果・テスト信号ログをリアルタイム表示。フィルタ＆エクスポート。
- **Status** (Dockable/Overlay)
  - Alias store パス、オンライン台数、現在の OSC endpoint、最後の送信結果などを表示。

- **起動アシスト**
  - 初回起動時にファイアウォール設定チェックリストをモーダル表示（`sudo nft insert ...` 等のコマンド例）。
  - `state/devices.json` が無ければ生成し、読み書きできない場合は警告。ログ保存先 (`logs/heartbeat.csv`) が存在しないときは自動生成。

## Interaction Flow
1. GUI polls `DeviceRegistry::snapshot()` every 500 ms on background thread (via `IoContextRunner`).
2. Heartbeat statuses classified:
   - `ok`: last sample < 3 s, latency mean < 50 ms.
   - `warning`: no heartbeat 3–10 s, or latency mean > 100 ms.
   - `critical`: >10 s or sequence gaps.
   Critical events generate log entries and optional desktop notification hook.
3. Renaming device:
   - Click alias -> inline edit, `Enter` to commit.
   - Update alias map + flush JSON.
   - Persist alias back into `DeviceRegistry` (so CLI sees same name) by calling `registerAnnounce` override when next heartbeat arrives.
4. Sending timeline:
   - Collect selected device IDs (or All toggle)。
   - Load timeline JSON (re-use `SoundTimeline::fromJsonFile`).
   - Bundle毎に `/preset/play` を補完し、リードタイム／BaseTime を UI から適用。
   - `OscSender` で送信し、成功・失敗・送信先をイベントログへ。
5. Test Signal:
   - Tileの `Test Signal` ボタンで短い WAV を指定デバイスに送信（プリセット例: `test_ping`）。
   - 送信結果をログへ記録し、Tileに「Last test: hh:mm:ss」を表示。

## Persistence
- `state/devices.json`: automatically maintained by monitor/registry.
- `state/device_aliases.json`: GUI maintains this. Schema example:
  ```json
  {
    "dev-004b12c48530": "FrontLeft",
    "dev-004b12c3c510": "FrontRight"
  }
  ```
- `config/gui_settings.json` (optional): store window layout, pagination size, last opened timeline path, OSC target host/port overrides.

## Threading Model
- Background `IoContextRunner` hosts `OscListener` and periodic registry polling.
- GUI thread pulls atomic snapshot of devices (copy of vector) to avoid locking in rendering.
- Dispatch commands post to background thread (enqueue to SPSC queue) so that socket operations do not block UI.

## Error Handling
- Heartbeat anomalies append to log, with button to export segment (CSV/JSON) for analysis.
- OSC send failures surfaced via toast + log entry (includes `errno` / message).
- Alias persistence failure (write error) prompts user to pick new location.

## Future Hooks
- Integrate `/sync/ping` + `/sync/adjust` handshake for fine-grained NTP alignment.
- Mini latency graph per device (sparkline) in tile; currently only aggregated stats.
- Multi-window layout (e.g., dedicated log window) once core grid stabilizes.
