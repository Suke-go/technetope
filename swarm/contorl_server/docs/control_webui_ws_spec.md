# Control Server ↔ Web GUI WebSocket 仕様

## 1. 接続情報
- **URL**: `ws://<control-server-host>:<port>/ws/ui`
- **サブプロトコル**: `toio-ui.v1`（省略可）
- **形式**: UTF-8 JSON テキスト
- **接続主体**: ブラウザ側 Web GUI がクライアント、制御サーバーがサーバー。

## 2. 共通メッセージ構造
| フィールド | 型 | 必須 | 説明 |
|-----------|----|------|------|
| `type` | string | ✅ | メッセージ種別。下表参照 |
| `payload` | object | ✅ | 種別ごとの本体 |
| `request_id` | string | 任意 | UI→Server コマンドに付けると `ack`/`error` で同値が返る |
| `timestamp` | number | 任意 | Server→UI 通知で送信時刻 (ms) を付加 |

未知の `type` を受信したサーバーは `type:"error"` を返却する。UI は知らない通知を無視可能にする。
※ Cube ID はすべて英数字 3 文字（例: `38t`, `j2T`, `d2R`, `534`）で表記する。フィールド境界はサーバー設定から読み込み、UIへ `field_info` または `snapshot.field` で通知する。

## 3. UI → Control Server コマンド

### 3.1 subscribe
購読対象ストリームやフィルタを設定。
```json
{
  "type": "subscribe",
  "request_id": "req-1",
  "payload": {
    "streams": ["relay_status", "cube_update", "log"],
    "cube_filter": ["38t", "h9Q"],
    "include_history": true
  }
}
```
- `streams`: 省略時は全種 (`relay_status`/`cube_update`/`fleet_state`/`log`).
- `cube_filter`: 空配列または未指定で全 Cube。
- `include_history: true` の場合、`snapshot` を即時返却。
- 初回 `subscribe` 後、サーバーは `field_info` を送出し、フィールドサイズを通知する。
- **応答**: `ack`。

### 3.2 manual_drive
モーター速度を直接指定。
```json
{
  "type": "manual_drive",
  "request_id": "drive-42",
  "payload": {
    "targets": ["38t", "j2T"],
    "left": 60,
    "right": 55
  }
}
```
- `targets`: 1 以上必須。Cube ID 文字列。
- `left/right`: -100〜100。
- **応答**: 成功で `ack`、失敗で `error`。

### 3.3 set_goal
ゴール位置を Fleet Orchestrator へ登録。
```json
{
  "type": "set_goal",
  "request_id": "goal-10",
  "payload": {
    "targets": ["d2R"],
    "goal": {"x": 150, "y": 200, "angle": 90},
    "priority": 5,
    "keep_history": true
  }
}
```
- `goal`: ミリメートル座標＋角度(度)。`angle` 省略可。
- **応答**: `ack` (`payload.details.goal_id` を返す)。

### 3.4 set_led
LED 色／パターン。
```json
{
  "type": "set_led",
  "request_id": "led-7",
  "payload": {
    "targets": ["38t"],
    "color": {"r": 255, "g": 80, "b": 0}
  }
}
```
- **応答**: `ack` / `error`。

### 3.5 set_group
UI 側で定義したグループをサーバーへ共有。
```json
{
  "type": "set_group",
  "request_id": "group-alpha",
  "payload": {
    "group_id": "alpha",
    "members": ["38t", "j2T", "h9Q"]
  }
}
```
- **応答**: `ack`。

### 3.6 request_snapshot
最新状態をまとめて取得。
```json
{
  "type": "request_snapshot",
  "request_id": "snap-1",
  "payload": {"include_history": false}
}
```
- **応答**: `snapshot` 通知 + `ack`。

## 4. Control Server → UI 通知

### 4.1 ack / error
```json
{"type":"ack","timestamp":1717000000000,"payload":{"request_id":"drive-42"}}
```
```json
{"type":"error","timestamp":1717000000500,"payload":{"request_id":"drive-42","code":"unknown_cube","message":"xyz"}}
```

### 4.2 relay_status
```json
{
  "type": "relay_status",
  "timestamp": 1717000001000,
  "payload": {
    "relay_id": "relay-a",
    "status": "connected",
    "rtt_ms": 35,
    "message": "Handshake complete"
  }
}
```

### 4.3 cube_update
複数更新をバッチ配信。各要素は以下を含む：
- `cube_id`, `relay_id`, `position`, `battery`, `state`, `goal_id`（任意）
- `led`: 現在の LED 設定 `{r,g,b}`。
```json
{
  "type": "cube_update",
  "timestamp": 1717000001100,
  "payload": {
    "updates": [
      {
        "cube_id": "38t",
        "relay_id": "relay-a",
        "position": {"x": 120, "y": 85, "deg": 30, "on_mat": true},
        "battery": 92,
        "state": "moving",
        "goal_id": "goal-10",
        "led": {"r": 255, "g": 80, "b": 0}
      }
    ]
  }
}
```

### 4.4 fleet_state
```json
{
  "type": "fleet_state",
  "timestamp": 1717000001200,
  "payload": {
    "tick_hz": 30,
    "tasks_in_queue": 4,
    "warnings": ["h9Q stalled"]
  }
}
```

### 4.5 log
```json
{
  "type": "log",
  "timestamp": 1717000001300,
  "payload": {
    "level": "warn",
    "message": "Relay relay-b reconnecting",
    "context": {"relay_id": "relay-b"}
  }
}
```

### 4.6 snapshot
`request_snapshot` や `subscribe.include_history` に対する応答。
- `field`: フィールド境界。
- `cubes[]`: `cube_update` と同型。初期表示で LED 色を反映するため `led` を必ず含める。
```json
{
  "type": "snapshot",
  "timestamp": 1717000002000,
  "payload": {
    "field": {
      "top_left": {"x": 45, "y": 45},
      "bottom_right": {"x": 455, "y": 455}
    },
    "relays": [
      {"relay_id": "relay-a", "status": "connected"},
      {"relay_id": "relay-b", "status": "connecting"}
    ],
    "cubes": [
      {"cube_id":"38t","relay_id":"relay-a","position":{"x":120,"y":80,"deg":30},"battery":92,"state":"idle","led":{"r":0,"g":255,"b":0}}
    ],
    "history": []
  }
}
```

### 4.7 field_info
フィールド境界の変更や初期通知に使用。
```json
{
  "type": "field_info",
  "timestamp": 1717000000800,
  "payload": {
    "top_left": {"x": 45, "y": 45},
    "bottom_right": {"x": 455, "y": 455}
  }
}
```
UI はこの値で Canvas のスケールや原点を決め、`snapshot.field` が届いた場合は上書きする。

## 5. エラーコード指針
| code | 意味 | 対応 |
|------|------|------|
| `invalid_payload` | 必須フィールド欠如 / 型不一致 | UI 側で入力値を見直す |
| `unknown_cube` | 指定した Cube ID が未登録 | Cube 割当設定を確認 |
| `relay_error` | Relay 未接続/送信失敗 | 接続状態を `relay_status` で確認 |
| `busy` | 既に処理中で一時的に拒否 | 一定時間後にリトライ |

## 6. 今後の拡張
- 位置マップ転送やログバイナリ化を行う場合は `type` を追加し、`toio-ui.v2` など別サブプロトコルで互換性を確保する。
- 認証が必要になった場合は WebSocket ハンドシェイクでトークン検証を追加する想定。
