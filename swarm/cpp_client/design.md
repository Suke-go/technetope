# C++ クライアント実装方針

## 1. 通信スタック
- WebSocket クライアントには `Boost.Beast`（`Boost.Asio` ベース）を採用し、非同期 I/O で `/ws` エンドポイントへ接続する。
- TLS や将来的なプロキシ対応を見据えて `io_context` + `ssl::context` 構成を想定。初回 `system` メッセージを受信してからコマンド送信を開始する。

## 2. JSON モデル
- `nlohmann::json` を利用してメッセージを直列化／逆直列化する。
- `CommandPayload`, `QueryPayload`, `ResultPayload`, `ResponsePayload` といった構造体を定義し、`to_json` / `from_json` を実装する。
- `require_result` や `notify` は `std::optional<bool>` で表現し、指定されたときのみ JSON に含める。

## 3. API 設計 (`ToioClient`)
- `connectCube(id)`, `disconnectCube(id)`, `move(id, left, right, requireResult=false)`, `setLed(id, r,g,b, requireResult=false)` などのメソッドを提供。
- `require_result=false` のときは fire-and-forget、`true` のときは `std::future<Result>` などで応答待ちを実装する。
- `queryBattery(id)` と `queryPosition(id, notify=false/true)` を用意し、購読フラグ付き `query` の送信を隠蔽する。

## 4. 受信処理とイベント
- WebSocket からの受信は専用スレッドまたは `asio::io_context` の strand 上で行い、JSON をパースして `type` ごとのハンドラに振り分け。
- `response(info=="position" && notify==true)` を受信したら購読中リスナーにプッシュ。`notify:false` が届いたら購読解除とみなす。
- `error` や `system` など接続状態に関わるメッセージはオブザーバへ通知して UI/CLI に表示。

## 5. オブザーバと購読管理
- `std::unordered_map<std::string, bool>` でターゲット毎の購読状態を保持。クライアント API からも参照できるようにする。
- コールバック登録用に `std::function<void(const PositionResponse&)> onPositionUpdate` などを `ToioClient` に設定可能にする。

## 6. エラーハンドリング / 再接続
- WebSocket 切断時は `Disconnected -> Reconnecting -> Connected` の状態遷移を管理し、再接続時に必要なら購読を再送。
- `result.status == "error"` や `response.message` は例外またはエラーコードで上位へ返し、ログにも出力する。

## 7. ビルド / 依存
- CMake プロジェクトを作成し、`find_package(Boost ...)`, `nlohmann_json` を `FetchContent` or `CPM.cmake` で取得。
- CLI 用には `CLI11` を追加し、サーバー URL / Toio ID / 操作モードなどを引数で指定できるようにする。

## 8. テスト
- Catch2 / GoogleTest で JSON 直列化とステートマシンの単体テストを書く。
- 統合テストでは Python リレーサーバーを立ち上げ、ループバック WebSocket 経由でコマンド送受信を確認する。

この方針に沿って `ToioClient` クラスと CLI ラッパーを実装し、WebUI と同様の機能（接続、移動、LED、位置購読）を C++ から制御できるようにする。
