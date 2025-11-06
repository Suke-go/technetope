# 実装分担 (最新版)

## Codex-A: 制御サーバー担当
1. プロジェクト骨組み (`CMakeLists.txt`, `include/`, `src/`, `third_party/`) を整備
2. 主要クラス実装: `relay_connection`, `relay_manager`, `cube_registry`, `fleet_orchestrator`, `ws_server`, `command_gateway`, `util/*`, `app/main`
3. `config_loader` で `field.top_left/bottom_right` を読み込み、デフォルト `(45,45)`→`(455,455)` を適用
4. CubeRegistry に LED 状態 `{r,g,b}` を保持し、`set_led` 実行時や Relay 応答に合わせて更新。`cube_update` / `snapshot` に `led` フィールドを含める
5. WebSocket (`command_gateway` + `ws_server`) を拡張し、`field_info` / `snapshot.field` / `cube_update.led` を配信
6. `docs/relay_json_spec.md`, `docs/control_webui_ws_spec.md` と整合をとりつつ `cmake -B build && cmake --build build` でビルド確認し、README に手順を追記

## Codex-B: WebUI担当
1. `webui/index.html`, `styles.css`, `main.js` でビルドレス SPA を実装（Design.md の要件に準拠）
2. WebSocket で `/ws/ui` に接続し、`field_info` / `snapshot.field` を受けて Canvas のスケール・原点を動的に設定
3. `cube_update.led` / `snapshot.cubes[].led` を用いて Cube グリッドやマップの色を更新し、LED フォーム（RGB のみ）と双方向に連動させる
4. 必要に応じて `scripts/mock_control_server.js` などを用意し、`field_info` や LED 更新を含むテストデータを配信できるようにする
5. WebUI の開発・起動手順を README に記載し、追加ライブラリなしの Vanilla JS/CSS を維持。UI 仕様に追加があれば `DESIGN.md` / `docs` を更新

共通: `DESIGN.md` と各仕様 (`docs/*`) を常に参照し、変更があれば同時に更新する。
