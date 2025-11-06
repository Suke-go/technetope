# Toio 制御サーバー設計書

## 1. 目的とスコープ
- 5 台の toio リレーサーバー（各 6 台接続可能）を束ね、合計 30 台の toio Cube を同時制御できる制御サーバーを C++ で構築する。
- 制御サーバーの機能を遠隔操作および可視化する WebUI を用意し、制御サーバーとは WebSocket のみで通信する。

## 2. システム全体構成
```
[Web Browser (WebUI SPA)]
        ↑↓ WebSocket(JSON)
[Toio Control Server (C++17)]
        ↑↓ WebSocket(JSON)
[Relay Server x5] -- BLE --> [toio Cube x30]
```
- WebUI はブラウザ内で実行される SPA を想定し、Control Server とは WebSocket のみでリアルタイム通信する。
- Control Server からリレー群へコマンドを転送し、各 Cube の状態を集約する。
- Control Server はマルチリレー接続、Cube 状態の一元管理、ログ／通知配信の役割を担う。
- 各 Cube の ID は英数字 3 文字（例: `38t`, `j2T`, `d2R`, `534`）で構成される。設定ファイル／UI／ログなどすべてこの ID をキーとして扱う。

## 3. フォルダ構成（シンプル版）
```
swarm/contorl_server/
├── CMakeLists.txt
├── DESIGN.md
├── config/
│   ├── control_server.example.json
│   └── control_server.json         # 実際に使う設定（git ignore 推奨）
├── include/
│   ├── app.hpp                     # main から呼び出すエントリポイント
│   ├── relay_connection.hpp
│   ├── relay_manager.hpp
│   ├── cube_registry.hpp
│   ├── fleet_orchestrator.hpp
│   ├── ws_server.hpp
│   ├── command_gateway.hpp
│   └── util/
│       ├── logging.hpp
│       └── config_loader.hpp
├── src/
│   ├── main.cpp
│   ├── app.cpp
│   ├── relay_connection.cpp
│   ├── relay_manager.cpp
│   ├── cube_registry.cpp
│   ├── fleet_orchestrator.cpp
│   ├── ws_server.cpp
│   ├── command_gateway.cpp
│   └── util/
│       ├── logging.cpp (任意: header-only なら不要)
│       └── config_loader.cpp
├── third_party/
│   └── nlohmann/json.hpp
├── webui/                          # index.html / main.js / styles.css
└── docs/
    ├── relay_json_spec.md
    └── control_webui_ws_spec.md
```
- C++ ファイルは「`.hpp` と `.cpp` がペアで同じ階層」に揃える。ネームスペースやクラスが増えても `include/`・`src/` を過剰に階層化しない。
- どうしてもモジュールを分けたい場合のみサブフォルダを切り、`include/util/logging.hpp` ↔ `src/util/logging.cpp` のように 1 段だけに留める。

### 3.1 主要ソースファイルと役割
| ファイル | 役割 | 主な公開 API |
|----------|------|--------------|
| `include/app.hpp` / `src/app.cpp` | `int run(const std::string& config_path);` など、main から起動されるアプリケーション制御。 | `int run(config_path)` |
| `relay_connection.{hpp,cpp}` | 単一リレーへの WebSocket クライアント。接続・再接続・メッセージ受信を担当。 | `start()`, `stop()`, `send(json)` |
| `relay_manager.{hpp,cpp}` | 複数リレーの管理と Cube ID の割当。UI からの命令を適切なリレーに転送。 | `send_manual_drive`, `send_led_command`, `set_event_callback` |
| `cube_registry.{hpp,cpp}` | `CubeState` のメモリ内キャッシュ。位置/バッテリー/LED などの状態を保持し、UI に渡すデータを整形。 | `apply(update)`, `get(id)`, `snapshot()` |
| `fleet_orchestrator.{hpp,cpp}` | ゴール割り当てと簡易な経路/衝突回避のロジック。 | `assign_goal(targets, pose)` |
| `ws_server.{hpp,cpp}` | WebUI との WebSocket サーバー。セッション管理と broadcast。 | `start()`, `stop()`, `broadcast(json)` |
| `command_gateway.{hpp,cpp}` | WebUI からの JSON メッセージを Relay/Fleet へ橋渡しし、`ack/error` を生成。 | `handle_message(json, session)` |
| `util/config_loader.{hpp,cpp}` | `config/control_server.json` を読み込み `struct ControlServerConfig` に変換。 | `ControlServerConfig load_config(path)` |
| `util/logging.hpp` | シンプルなロガー (chrono + std::cout)。ヘッダーオンリー。 | `log::info`, `log::warn` など |

このように各モジュールを 1 ファイルずつに収めれば、規模が大きくなっても見通しやすい。サブフォルダで細分化したくなった場合も、`include/relay/relay_connection.hpp` → `src/relay/relay_connection.cpp` のように `.hpp/.cpp` のペアを崩さないことをガイドラインとする。

## 4. 使用ライブラリ / ツール
| 分類 | ライブラリ / ツール | 用途 / 備考 |
| ---- | ------------------- | ----------- |
| ビルド | CMake (>=3.18) | クロスプラットフォームビルド。`cmake -B build` で生成 |
| コンパイラ | Clang++ / g++ (C++17 対応) | AppleClang 17 / GCC 11 以上想定 |
| ネットワーク | Boost.Asio / Boost.Beast | HTTP/WebSocket 実装、Relay接続および WebUI サーバー |
| JSON | nlohmann/json (single-header) | 設定ファイル / WebSocket メッセージのシリアライズ |
| ログ | 独自 util/logging (std::cout ベース) | 依存軽量化のため自前実装 |
| WebUI | 素の HTML/CSS/JS (ES Modules) | `webui/` 配下でシングルページ構成 |

※ Boost は brew などのパッケージマネージャで事前インストールしておく。ヘッダーオンリー JSON は `third_party/` に同梱する。

## 5. ビルド / 実行手順
1. 依存インストール: Boost(Asio/Beast), CMake, C++17 コンパイラ。
2. 設定ファイルの作成:
   ```bash
   cd swarm/contorl_server
   cp config/control_server.example.json config/control_server.json
   # relay URI / cube ID / UI ポートを編集
   ```
3. ビルド:
   ```bash
   cmake -B build
   cmake --build build
   ```
4. 起動:
   ```bash
   ./build/toio_control_server config/control_server.json
   ```
5. WebUI クライアントは `webui/index.html` をブラウザで開く（もしくは簡易な静的サーバーで配信）し、`ws://<control-server-host>:<port>/ws/ui` に接続する。

- Relay サーバー側の JSON API 仕様 (`docs/relay_json_spec.md`) と WebUI 通信用 WebSocket 仕様 (`docs/control_webui_ws_spec.md`) を参照し、実装・テスト時のメッセージ内容を合わせる。
- シミュレーションやモックが必要な場合は、簡易 Relay モックサーバーを `tools/relay_mock/` として追加予定。

## 6. フィールド設定
- `config/control_server.json` にフィールド境界を追加する。
  ```jsonc
  {
    "field": {
      "top_left":  {"x": 45,  "y": 45},
      "bottom_right": {"x": 455, "y": 455}
    },
    "ui": { ... },
    "relays": [ ... ]
  }
  ```
  - `top_left` / `bottom_right` はミリメートル座標で指定。省略時はデフォルトで `(45,45)` → `(455,455)` を使用。
  - 制御サーバー起動時に読み込み、`FieldConfig` として保持する。`cube_update` の正規化や UI への送信時に利用。
  - WebUI には `snapshot` もしくは専用 `field_info` 通知で境界を配信し、Canvas の表示サイズやスケールを同期させる。
- 今後フィールドが拡張／回転する場合もこの設定で吸収可能にする。例えば `top_left` を `(0,0)`、`bottom_right` を `(600,600)` に変えるだけで UI も自動でリサイズされる。

## 7. WebUI 機能要件
最初のリリースでは以下の機能を備えたシングルページ UI を目指す。

| 項目 | 内容 | 補足 |
|------|------|------|
| ヘッダー / ステータス表示 | 接続状態（`connected/connecting/disconnected`）とサーバー時間を表示。 | WebSocket 再接続時はアイコンや色で通知。 |
| Relay Monitor | 全リレーの接続状態、遅延(Ping RTT)、再接続回数、直近エラーを一覧表示。個別の接続/切断ボタン。 | `relay_status` 通知をそのまま反映。 |
| Cube Grid | 30 台までをカード状に表示（ID、所属リレー、バッテリー、LED 色、現在ステータス、選択チェックボックス）。 | フィルタ/検索ボックス付き。LED は `cube_update.led` を表示し、サーバーが保持している最新値を同期する。 |
| フィールドマップ | Canvas で 2D 位置を描画。選択 Cube は LED 色で強調し、ゴール座標やトレイルをオーバーレイ。 | 拡大縮小・パン操作をサポート。 |
| Manual Control パネル | 選択 Cube に対してジョイスティックまたは左右スライダーで速度指令を送信。 | `manual_drive` コマンドを発行。 |
| Goal/LED 設定フォーム | ゴール座標 (`x,y,angle`) 入力欄と LED 色 (RGB)。複数 Cube/グループに対して送信。 | `set_goal`, `set_led` を利用し、応答後にグリッド/マップへ即時反映。 |
| グループ管理 | グループ ID とメンバーを登録・更新できるテーブル。 | `set_group` をバックエンドへ送信。 |
| ログ・アラートビュー | `log` と `error` 通知をレベル別に色分けして流す。フィルタとクリアボタン。 | 重要アラートはトーストでポップアップ。 |
| Snapshot / Refresh | 全体状態をリロードする「Refresh」ボタン。押下で `request_snapshot` を送る。 | 初回接続時や `include_history` 指定時も snapshot を表示。 |
| 設定ダイアログ (任意) | WebSocket エンドポイントや UI 設定をブラウザ側で変更。 | 将来の複数環境対応を見据えて用意。 |

### 6.1 画面レイアウト例
```
┌───────────────────────────────────────────────┐
│ Header: Server Status / Connect Button                              │
├─────────────┬──────────────────────────────────────────────────────┤
│ Relay Monitor│ Field Canvas (中心)                                  │
│ (list)       │                                                      │
├─────────────┼──────────────────────────────────────────────────────┤
│ Cube Grid    │ Control Drawer (Manual, Goal, LED, Groups)           │
├─────────────┴──────────────────────────────────────────────────────┤
│ Log / Alert Panel                                                   │
└────────────────────────────────────────────────────────────────────┘
    ※ Canvas の幅・高さは `field_info` / `snapshot.field` で渡された境界を元にダイナミックに決まる。
```

### 7.2 UI 技術スタック
- HTML/CSS/JavaScript (ES Modules)。ビルド不要、`webui/main.js` から初期化。
- 状態管理は素の `EventTarget` ベースで実装し、`cube_update` 等の通知を購読して DOM を更新。
- 描画ライブラリは使用せず、Canvas API のみでマップを描くことで依存を抑える。

### 7.3 動作フロー
1. WebSocket 接続確立 → `subscribe`（`include_history:true`）送信。
2. `field_info` と `snapshot.field` を受信し、Canvas のスケールを設定。同時に `snapshot.cubes[].led` で LED 状態を初期化し、Cube Grid / マップに反映。
3. 以降、`cube_update` や `relay_status` を受けて UI を更新。
4. ユーザー操作（ジョイスティック等）でコマンド送信 → `ack/error` をトースト表示。
5. エラーや切断時は自動リトライし、操作を一時的に無効化する。

これらの機能を `webui/` ディレクトリに配置した `index.html`, `styles.css`, `main.js` で実装し、開発初期はローカルファイルを直接ブラウザで開くだけで動作する構成とする。
