# Toio Relay C++ クライアント ミニ仕様

Python 製 Toio リレーサーバー (`swarm/relay_server/server.py`) と C++ 制御サーバーの間をつなぐ最小限のクライアントライブラリ仕様を定義する。目的は「複数リレーに同時接続し、Cube を操作／状態取得できること」のみで、余計な抽象化や複雑なロジックは持たない。

---

## 1. ゴール
- RelayConnection が WebSocket 1 本を担当し、JSON メッセージを送受信する。
- RelayManager が複数リレーの RelayConnection を束ね、Cube ID 単位で状態をキャッシュする。
- Cube の状態はシンプルな構造体 (`CubeState`) で表現し、必要なときに読み取れる。
- 追加機能（メトリクス、詳細なイベント、複雑な購読管理）は実装しない。必要になったら別仕様で拡張する。

---

## 2. コンポーネント
```
┌────────────────────────┐
│ Control Server (C++)   │
│  ┌──────────────────┐  │
│  │ RelayManager     │◄─┴─ 上位アプリから設定注入
│  └──────┬──────────┘
│         │ owns N RelayConnection
│  ┌──────▼──────────┐       ws://<host>:8765/ws
│  │ RelayConnection │ ─────────────────────────► Python Relay Server
│  └──────┬──────────┘       (JSON / WebSocket)
│         │ emits CubeState updates
│  ┌──────▼──────────┐
│  │  CubeState Map  │
└──┴─────────────────┴───────────────────────────────────────────────
```
- **RelayConnection**: 接続確立、送信キュー、受信ループのみ担当。
- **RelayManager**: Cube ID → RelayConnection のルーティング、シンプルな状態管理、上位 API 提供。
- **CubeState**: Cube の最新 `connected`, `battery_level`, `position` を保持するだけの POD。

---

## 3. 依存関係 / ビルド
- C++17 以上（`std::optional`／`std::chrono` 利用）。
- Boost.Asio + Boost.Beast (WebSocket クライアント)。
- `nlohmann::json` で JSON シリアライズ。
- 追加ライブラリやコルーチンは不要。

---

## 4. 設定注入
ライブラリは設定ファイルを読まない。上位アプリが任意の方法で取得し、以下の構造体に渡す。

| フィールド | 型 | 必須 | 説明 |
|-----------|----|------|------|
| `id` | std::string | ✅ | RelayConnection の一意 ID |
| `uri` | std::string | ✅ | WebSocket URL (`ws://host:port/ws`) |
| `cubes` | std::vector<std::string> | ✅ | このリレーにぶら下がる Cube ID |
| `connect_on_start` | bool | 任意 (既定 true) | RelayManager 起動時に `cmd:"connect"` を送るか |

---

## 5. 対応する JSON メッセージ
- **command**: `connect`, `disconnect`, `move`, `led`
- **query**: `battery`, `position`（購読は `notify:true/false` に対応するが内部では単純に bool を保存するだけ）
- **result / response**: サーバーからの返信をそのまま `nlohmann::json` で保持し、必要フィールドのみ参照
- **system / error**: ログに吐いて接続再試行するだけ。詳細なハンドラは不要。

詳細な JSON 形式は共通仕様 (`docs/toio_relay_json_spec.md`) を参照し、ここではバリデーションを最小限に留める。

---

## 6. RelayConnection API
```cpp
class RelayConnection {
 public:
  RelayConnection(asio::io_context& ctx, RelayConnectionConfig config);

  void Start();          // 非同期で接続開始。成功時 system.connected を待って Ready 扱い。
  void Stop();           // WebSocket Close。自動再接続は行わない（必要なら Start を再呼び出し）。

  std::future<ResultPayload> SendCommand(CommandPayload cmd);
  std::future<ResponsePayload> SendQuery(QueryPayload query);

  void SetMessageCallback(std::function<void(const RelayEnvelope&)> cb);
};
```
- 受信した JSON は `RelayEnvelope` としてコールバックへ渡し、RelayManager が解釈する。
- Ping/Pong・指数バックオフ等は実装しない。接続が落ちたら上位が `Start()` を再度呼ぶ。

---

## 7. RelayManager API
```cpp
class RelayManager {
 public:
  RelayManager(asio::io_context& ctx, std::vector<RelayConnectionConfig> configs);

  void Start();  // 全 RelayConnection の Start を呼び、必要なら connect コマンドを送る
  void Stop();   // 全 RelayConnection の Stop

  RelayConnection* GetRelayForCube(std::string_view cube_id);

  std::future<ResultPayload> SendCommand(std::string_view cube_id, CommandPayload cmd);
  std::future<ResponsePayload> RequestBattery(std::string_view cube_id);
  std::future<ResponsePayload> RequestPosition(std::string_view cube_id, bool notify);

  CubeState GetCubeState(std::string_view cube_id) const;
  std::vector<CubeState> GetAllCubeStates() const;
};
```
- RelayManager は `RelayConnection::SetMessageCallback` を使い、`response/result` を受けて対象 CubeState を更新するだけ。
- Cube が購読中かどうかは `CubeState::position_notify` で表現する。

---

## 8. CubeState
```cpp
struct CubeState {
  std::string cube_id;
  std::string relay_id;
  bool connected = false;

  std::optional<int> battery_level;
  std::optional<Position> last_position;  // {int x, int y, int angle, bool on_mat}
  bool position_notify = false;

  std::chrono::steady_clock::time_point last_update{};
};
```
- RelayManager が `SendCommand(connect)` 成功を受けたら `connected=true`。
- `Device not connected` エラーを受けたら `connected=false` に戻す。
- 追加フィールドは必要になったら拡張する。

---

## 9. 基本フロー
1. 上位アプリが設定を読み、RelayManager を作成→`Start()`。
2. RelayManager は各 RelayConnection を起動し、`connect_on_start=true` の Cube に `command:connect` を投げる。
3. 制御ロジックが `SendCommand(cube_id, move)` を呼ぶと、RelayManager は対象リレーを引いて RelayConnection 経由で送信。`result` を future で待ち、成功時は CubeState を更新。
4. 位置が必要なときは `RequestPosition` を呼び、`notify:true` を指定すると RelayManager が `position_notify` を true に保つ。Push 通知は Callback 経由で受けて `last_position` を更新する。

---

## 10. テストの目安
1. **ユニット**: `CubeState` 更新ロジック（battery/position/接続フラグ）。
2. **接続テスト**: Python リレーサーバーをローカルで立ち上げ、`connect → move → position` の往復ができること。
3. **切断テスト**: リレーサーバーを落として `Start()` を再呼び出し、復帰後に再度コマンドが通ること。

以上の最小構成を先に実装し、足りない機能（自動再接続、詳細イベント、メトリクス等）は必要が判明してから別途拡張する。
