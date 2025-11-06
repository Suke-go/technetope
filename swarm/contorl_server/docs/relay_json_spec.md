# Toio Relay JSON API 概要

既存の Python 製リレーサーバー (`relay_server/server.py`) が提供する WebSocket API の最小まとめです。詳細な全文は `swarm/relay_server_json仕様書.md` を参照してください。

## 1. 接続
- URL: `ws://<relay-host>:8765/ws`
- プロトコル: WebSocket (JSON テキスト)
- 接続直後にサーバーから `type:"system"` / `status:"connected"` が届く。

## 2. メッセージ共通構造
```json
{
  "type": "command | query | result | response | system | error",
  "payload": { ... }
}
```

## 3. クライアント → リレー
### command
| フィールド | 内容 |
|-----------|------|
| `cmd` | `connect` / `disconnect` / `move` / `led` |
| `target` | Toio ID（英数字3文字、例: `38t`, `j2T`, `d2R`） |
| `params` | `move`: `{left_speed,right_speed}` / `led`: `{r,g,b}` |
| `require_result` | true でレスポンス必須、false で成功時省略 |

### query
| フィールド | 内容 |
|-----------|------|
| `info` | `battery` / `position`（将来的に `led` などを追加する余地あり） |
| `target` | Toio ID（英数字3文字） |
| `notify` | `info:"position"` のとき購読用 (true=購読開始, false=停止) |

## 4. リレー → クライアント
### result
`command` に対する同期応答。
```json
{
  "type": "result",
  "payload": {"cmd":"move","target":"38t","status":"success"}
}
```
`status:"error"` の場合は `message` に理由が入る。

### response
`query` の結果。
- `info:"battery"`: `battery_level` を返す
- `info:"position"`: `position {x,y,angle,on_mat}` を返す。`notify:true` で継続通知
- `info:"led"`（導入する場合）: `led {r,g,b}` を返す

### system / error
- `system`: 接続状態 (`connected`, `started`, `stopped`, `error` etc.)
- `error`: 不正なメッセージ形式など。`payload.message` に詳細。

## 5. サンプル
```json
// move
{"type":"command","payload":{"cmd":"move","target":"38t","params":{"left_speed":50,"right_speed":60}}}

// position subscribe
{"type":"query","payload":{"info":"position","target":"38t","notify":true}}

// position notify
{"type":"response","payload":{"info":"position","target":"38t","notify":true,"position":{"x":120,"y":200,"angle":90,"on_mat":true}}}
```

---
この概要をベースに、制御サーバー側の RelayConnection / RelayManager を実装する。
