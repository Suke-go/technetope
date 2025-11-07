# Toio C++ Client Specification

この文書は `swarm/cpp_client` 以下に実装された Toio 中継サーバー向け C++ クライアントの全体像をまとめた仕様書です。`README.md` がセットアップ手順を、`design.md` が実装方針を、そして本書が完成したソリューションの構造と挙動を説明します。

## 1. 目的と適用範囲

- Boost.Asio/Beast を用いた WebSocket クライアントを提供し、Toio Relay Server (`/ws`) とのコマンド／クエリ通信を CLI から操作できるようにする。  
- 単一 Cube ID を主対象としつつ、ターゲット識別子を呼び出し側で差し替えることで複数 Cube も制御可能。  
- 仕様対象に含まれるのはライブラリ `toio_client` と CLI アプリ `toio_cli` の双方であり、サーバーや他言語クライアントは扱わない。

## 2. ハイレベルアーキテクチャ

```text
┌────────────────────────────┐
│        CLI (main.cpp)      │
│  - 引数解析/REPL           │
│  - ユーザー入力→API呼び出し│
└────────────┬──────────────┘
             │ ToioClient API
┌────────────▼──────────────┐
│     ToioClient (lib)       │
│  - WebSocket接続管理       │
│  - JSON送受信              │
│  - コマンド/クエリ構築     │
│  - Handler発火             │
└────────────┬──────────────┘
             │ Boost.Asio/Beast
┌────────────▼──────────────┐
│  Toio Relay Server (/ws)   │
└────────────────────────────┘
```

## 3. コンポーネント仕様

### 3.1 ToioClient (`include/toio/client/toio_client.hpp`, `src/toio_client.cpp`)

| 要素            | 説明 |
|-----------------|------|
| 依存            | `boost::asio`, `boost::beast::websocket`, `nlohmann::json` |
| 接続状態管理    | `connected_`/`running_` のアトミック変数で WebSocket のライフサイクルを追跡し、受信スレッドの起動/停止を制御。 |
| API             | `connect/close`, `send_command`, `send_query`, 高水準ラッパー (`connect_cube`, `send_move`, `set_led`, `query_position` など)。 |
| ハンドラ        | `set_message_handler`, `set_log_handler` でアプリケーション固有処理を注入。設定が無ければ標準出力ログにフォールバック。 |
| 送信処理        | JSON をシリアライズして `websocket_.write` に投入。`write_mutex_` で直列化し、失敗時は `beast::system_error` を送出。 |
| 受信処理        | `reader_loop` が `flat_buffer` を共有しつつブロッキング読み込み→JSON 解析→ハンドラ発火。解析失敗はログ出力に留め接続は維持。 |

### 3.2 CLI (`src/main.cpp`)

- 起動引数: `--id` は複数回指定でき、`--host`, `--port`, `--path`, `--subscribe` が任意。`--help/-h` は使用方法を表示して終了。  
- セッション初期化: `ToioClient` を生成→ログ／メッセージハンドラ設定→`connect()`→登録された全 Cube へ `connect_cube()`→必要なら各 Cube へ `query_position(..., true)` 。  
- REPL コマンド:
  - `use <cube-id>`: 以降の操作対象 Cube を切り替える。未登録 ID を指定した場合は即座に登録され、必要に応じて `connect` を実行する。  
  - `connect` / `disconnect`
  - `move <L> <R> [require]` / `moveall <L> <R> [require]` (後者は既知 Cube 全体へ送信)
  - `stop` (= `move 0 0`)
  - `led <R> <G> <B>` / `ledall <R> <G> <B>` (後者は既知 Cube 全体へ送信)
  - `battery`, `pos`
  - `subscribe`, `unsubscribe` (Notify フラグの on/off)
  - `help`, `exit`, `quit`
- 終了時は `disconnect_cube`→`close` の順に呼び、受信スレッドを確実に join。

## 4. メッセージモデル

### 4.1 Command

```json
{
  "type": "command",
  "payload": {
    "cmd": "<connect|disconnect|move|led|...>",
    "target": "<cube-id>",
    "params": { ... },            // 未指定時は空オブジェクト
    "require_result": true|false  // 任意
  }
}
```

- `params` 例: `move` は `{"left_speed":-30,"right_speed":30}`, `led` は `{"r":255,"g":0,"b":0}`。  
- `require_result` を `false` にするとレスポンス待ちを省略し、サーバー側で `result` を送らない運用と合わせる。

### 4.2 Query

```json
{
  "type": "query",
  "payload": {
    "info": "<battery|position|...>",
    "target": "<cube-id>",
    "notify": true|false  // 任意
  }
}
```

- `notify=true` は購読開始、`notify=false` は購読解除を意味する。省略すると単発クエリ。  
- クエリ応答 (`response`) やコマンド結果 (`result`) は生 JSON で `MessageHandler` に渡され、CLI では `[RECV] ...` として標準出力に表示される。

## 5. 接続ライフサイクル

1. `connect()`  
   - DNS 解決 (`resolver_.resolve`) → TCP 接続 (`asio::connect`) → WebSocket ハンドシェイク。  
   - User-Agent を `toio-cpp-client/0.1` に設定。  
   - `connected_` を立て、`reader_thread_` を起動。
2. `close()`  
   - `running_` を落とし、`websocket_.close(normal)`。  
   - 受信スレッド join 後に `connected_` を false。  
   - エラーはログへ出しつつ例外は投げない。

接続前 API 呼び出しは `ensure_connected()` が `runtime_error` を投げて防止する。

## 6. エラーハンドリングとログ

- CLI コマンド実行中の例外は `Command error: <what>` としてユーザーへ表示。  
- 致命的例外は `Fatal error` メッセージとともに `parse_options` の usage を再掲してプロセス終了。  
- `reader_loop` 中の WebSocket エラーは `log_handler` 経由で通知され、接続はクリーンに閉じられる。  
- JSON 解析失敗時は `Failed to parse JSON: ...` をログするが、後続処理は継続。

## 7. ビルド & 依存管理

- CMake 3.20+ / C++20 / clang++ 15+ 推奨。  
- `Boost_NO_BOOST_CMAKE` を有効化して `find_package(Boost 1.78 REQUIRED)` を使用。  
- `nlohmann_json` はシステム導入を優先し、無い場合は `FetchContent` で `v3.11.3` を取得。  
- 出力物:  
  - `libtoio_client.a` (またはプラットフォーム相当)  
  - CLI 実行ファイル `toio_cli`

## 8. 拡張ポイントとベストプラクティス

- **複数 Cube 対応**: `use <cube-id>` でアクティブターゲットを切り替えつつ、内部では `std::unordered_map<std::string, bool>` などで購読状態を保持する。要件次第ではコマンドごとに明示ターゲット指定を許可するなど UI を拡張する。  
- **TLS/プロキシ**: `ToioClient` は `io_context` とソケット層を抽象化しているため、`websocket_t` を SSL ストリームに置き換えることで拡張可能。  
- **高度な処理**: `MessageHandler` で JSON をドメインモデルに変換し、GUI や別サービスへの転送、メトリクス収集などを追加する。  
- **テスト**: JSON 生成ロジックは純粋関数に近いため、Catch2/GoogleTest で `send_command` の出力を検証する単体テストが実装しやすい。

---

本仕様に沿ってコードを読むことで、CLI からの操作シナリオやライブラリの再利用方法、拡張時の考慮点までを一望できます。
