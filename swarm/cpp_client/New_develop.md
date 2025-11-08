# これからの実装計画

## toio::api::FleetControl の導入

- `include/toio/api/fleet_control.hpp` に薄いラッパー `toio::api::FleetControl` を追加。  
  - C++側から `FleetManager` と `GoalController` を直接扱わずに、状態参照 (`snapshot()`)、明示的な接続制御 (`connect`/`disconnect`)、目標指定 (`start_goal/update_goal/stop_goal/stop_all_goals()`)、LED制御 (`set_led()`) を呼び出せる。  
  - `update_goal()` 経由でゴールタスクを止めずに目標座標だけを更新できるよう、`GoalController` 内部の目標共有・自動停止フラグを実装。  
  - 手動移動コマンドも `move()` で呼び出せるため、円運動など任意の速度制御が可能。  
  - `set_state_callback` / `set_message_callback` で従来通りの通知を上位エンジンへ渡せる。  
  - ゴール制御のログは `set_goal_logger` で任意ハンドラに差し替え可能。
- `toio_lib` が `goal_controller.cpp`, `config_loader.cpp`, `api/fleet_control.cpp` を取り込み、`yaml-cpp` もリンク対象になった。  
  ライブラリをリンクすれば CLI 以外からも YAML 設定→FleetControl 生成まで完結する。

## 典型的な使用例

```cpp
#include "toio/api/fleet_control.hpp"
#include "toio/cli/config_loader.hpp"

void sample() {
  toio::cli::Options opt;
  opt.fleet_config_path = "configs/fleet.yaml";
  auto plan = toio::cli::build_fleet_plan(opt);

  toio::api::FleetControl control(plan.configs);
  control.start(); // set_led/start_goal 呼び出し前に開始（未開始なら内部で自動起動）

  // 状態取得（status コマンド相当）
  auto snaps = control.snapshot();
  for (const auto &snap : snaps) {
    // snap.state.server_id / cube_id / connected / battery / position ...
  }

  // 目標指定
  toio::control::GoalOptions goal;
  goal.goal_x = 200;
  goal.goal_y = 350;
  control.start_goal("cube-a01", goal);

  // LED 制御
  toio::middleware::LedColor red{255, 0, 0};
  control.set_led("cube-a01", red);
}
```

## 今後検討したい点

- `FleetControl` からバッテリー・位置問い合わせ API など他コマンドも順次エクスポートする。  
- ルート決定エンジンとのインターフェース仕様（同期/非同期やイベント通知方法）を要検討。  
- CLI バイナリをサンプルへ縮小 or 廃止するなら、`main.cpp` を差し替えて `FleetControl` 利用例に置き換える。

## 実行可能サンプル

- `samples/fleet_control_sample.cpp` を追加。  
  - `--fleet-config <path>` を受け取り、最初のキューブの LED を変更しつつゴールタスクを開始、最新スナップショットを一定間隔で出力する簡易サンプル。  
  - ビルドターゲット名: `fleet_control_sample`。`cmake --build build --target fleet_control_sample` で生成後、`./build/fleet_control_sample --fleet-config configs/fleet.yaml` のように実行する。
- `samples/circle_motion_sample.cpp` を追加。  
  - 中心 `(250, 250)` を基準に、半径を時間でゆっくり振動させつつ、一定間隔で進行方向を反転するデモ。`FleetControl::connect()` で各キューブを一台ずつ接続→成功を確認し、接続済みの機体だけを対象に `FleetControl::update_goal()` で位相ずれゴールを更新し続けて円環上を滑らかに走行させる。  
  - 実行前に各機体ごとに接続完了を待機し、タイムアウトした機体はその都度ログに記録した上でスキップする。
  - ビルドターゲット名: `circle_motion_sample`。`cmake --build build --target circle_motion_sample` の後、`./build/circle_motion_sample --fleet-config configs/fleet.yaml` で実行。
