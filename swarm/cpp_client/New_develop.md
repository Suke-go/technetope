# これからの実装計画

## main.cpp リファクタリング方針

### 実装スコープ
今回のリファクタでは以下のみ実施する。Planning 層の実装や REST/GUI への展開は含めない。
1. `main.cpp` から FleetManager 依存コードを `FleetHub` に抽出し、CLI 以外の UI からも再利用できる「操作ハブ」を導入する。
2. CLI のコマンドをテーブル形式（名前／ヘルプ／ハンドラ）で定義し、`main.cpp` はトークン化 + ディスパッチのみを行う。
3. `main.cpp` は入出力（Usage 表示・REPL ループ・`exit/quit` 判定）のみに責務を限定する。

### レイヤー分離の狙い
- **CLI 入出力 (`src/main.cpp` + `src/cli/repl*` など)** … ユーザー入力と出力のみを扱う。REPL 処理に専念。
- **Controllers (`FleetHub`)** … FleetManager を介した操作ロジックを集約 (`use`, `move`, `status`, `batteryall` など)。CLI 以外の UI からも再利用できるインターフェース。
- **Command 定義層** … CLI コマンド名とハンドラをテーブル化し、`main.cpp` はコマンドディスパッチのみ行う。
- **Planning 層** … 今回は未実装。将来的に `snapshot()` を用いた自動経路生成や協調制御をこのレイヤーに実装する。

### 新規フォルダ／ファイル構成（予定）
- `src/controllers/fleet_hub.cpp` / `include/toio/controllers/fleet_hub.hpp`  
  - FleetManager への操作をまとめる。アクティブターゲット管理、購読状態、`snapshot()` ラッパーなどを担う。表示は上位層（CLI 側）が `auto snapshots = hub.snapshot();` で受け取って整形する。
- `src/cli/command_registry.cpp` / `include/toio/cli/command_registry.hpp`  
  - `struct CommandSpec { std::string name; std::string help; handler }` 形式でコマンドを登録。
  - `using CommandMap = std::unordered_map<std::string, CommandSpec>;`
  - `CommandSpec::handler` は `std::function<void(const std::vector<std::string> &)>` を想定。
- （オプション）`src/cli/repl.cpp` / `include/toio/cli/repl.hpp`  
  - REPL 入力の読み取りと終了判定を切り出す場合に使用。

### main.cpp の責務
- `parse_options()` で CLI 引数を解析し、FleetManager/FleetPlan/FleetHub を初期化する。
- REPL ループを維持しつつ、入力を `tokenize()` → `CommandMap` に渡してハンドラを実行する。
- `exit` / `quit` の判定、Usage 表示 (`print_usage`) など、CLI 固有の I/O のみに専念。
- FleetManager との直接的なやり取り（`manager.move(...)` 等）はすべて FleetHub 経由にする。

### FleetHub の責務
- FleetManager と設定 (`ServerConfig`) を受け取り、アクティブターゲット・購読状態の管理、全 Cube への一括操作を提供する。
- サポートする主な機能:
  - ターゲット管理: `use_cube_token(token)`, `active_target()`。
  - 単一操作: `connect_active`, `disconnect_active`, `move_active`, `stop_active`, `set_led_active`, `query_battery_active`, `query_position_active(notify)`, `subscribe_active`, `unsubscribe_active`。
  - 全体操作: `move_all`, `set_led_all`, `query_battery_all`, `query_position_all`, `subscribe_all`, `unsubscribe_all`。
  - 状態提供: `snapshot()`（CLI 側が `auto snapshots = hub.snapshot();` として表示を担当）。
- CLI 以外の UI からも呼び出せるよう、標準出力への依存は持たない。必要に応じて上位から `std::ostream&` を渡す設計ができるようにしておく。

### Command テーブルの構造
```cpp
struct CommandSpec {
  std::string name;
  std::string help;
  std::function<void(const std::vector<std::string>&)> handler;
};
using CommandMap = std::unordered_map<std::string, CommandSpec>;
```
- `build_command_map(FleetHub&)` でテーブルを組み立てる。
- `print_help(const CommandMap&, std::ostream&)` で一覧表示。
- `main.cpp` の REPL では `CommandMap` からハンドラを取得し、必要ならエラーを表示する。

### 実装ステップ（今回扱う範囲）
1. ファイルレイアウトを上記の通り作成し、CMake へ追加。`toio_lib` が新ディレクトリのソースを含むようにする。
2. `FleetHub` に操作ロジックを移植し、`main.cpp` の FleetManager 直接呼び出しを削除。
3. `command_registry` を実装して REPL ループで呼ぶようにする。
4. （将来）Planning 層や REST/GUI 連携を追加する際は `FleetHub` を経由して実装する。

この構成により、CLI のメンテナンス性を向上させつつ将来の上位層拡張（経路生成や協調制御）へもスムーズに移行できるようにする。

この構成により、CLI のメンテナンス性を上げつつ将来の上位層拡張 (経路生成や協調制御) を見据えたコードベースに移行できる。
