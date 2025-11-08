# これからの実装計画

## main.cpp リファクタリング方針

### レイヤー分離の狙い
- **CLI 入出力** … ユーザー入力と出力のみを扱う。REPL の入出力処理はこの層に閉じ込める。
- **Controller 層** … FleetManager を介した操作ロジックを集約 (`use`, `move`, `status`, `batteryall` など)。CLI 以外の UI (REST/gRPC/GUI) からも再利用できるインターフェースにする。
- **Command 定義層** … CLI コマンド名とハンドラをテーブル化し、`main.cpp` はコマンドディスパッチのみ行う。テスト可能なハンドラ単位で機能を整理。
- **Planning 層** … 今後の経路計画・協調制御に備え、FleetManager の `snapshot()` を入力にルート生成や任務スケジューリングを行うモジュールを独立させる。

### 推奨ディレクトリ案
- `src/cli/`  
  - `repl.cpp`/`repl.hpp`: 標準入力読み取り・help 表示などの純粋な CLI UI。
  - `commands/`: `struct Command { name, help, handler }` をまとめる。
- `src/controllers/`  
  - `fleet_hub.hpp/cpp`: FleetManager を保持し、単一 Cube/全 Cube 操作 API を提供。
- `src/planning/`  
  - `route_planner.hpp/cpp`, `mission_runner.hpp/cpp` など経路計画や協調制御のロジック。

### 実装ステップ
1. `main.cpp` から FleetManager 依存コードを `FleetHub` に抽出。CLI は `fleet_hub.move(...)` のように呼び出すだけにする。
2. コマンドテーブル (`commands/registry.cpp` 等) を作成し、`main.cpp` はトークン化 → コマンド解決 → ハンドラ呼び出しのみ。
3. (将来)Planning 層の枠組み (`RoutePlanner`, `MissionRunner`) を追加し、`FleetHub` 経由で move/subscribe を発行できるようにする。今回のリファクタ範囲には含めない。
4. (将来) ユーザー I/O (REPL) と Controller を分離したことで、REST/gRPC といった別 UI から Controller を利用できる構造に拡張。

この構成により、CLI のメンテナンス性を上げつつ将来の上位層拡張 (経路生成や協調制御) を見据えたコードベースに移行できる。
