# PC Tools Module

Utilities for the sequencing, monitoring, and real-time control workflow live here.  
PCツール群は C++17 + CMake を想定した再構築中であり、以下のサブモジュールに分割する。

- `scheduler/`: タイムライン編集、OSCバンドル生成、コマンド送出 (`liblo`/`oscpack`, `nlohmann/json`, `CLI11`, `spdlog`)
- `monitor/`: 遅延計測・心拍受信・ログ集約 (`Boost.Asio`, `spdlog`, `csv-parser`)、暗号層は `OpenSSL`/`Mbed TLS`/`libsodium` のいずれか
- `libs/`: 共有ユーティリティ（暗号ラッパー、OSC抽象、設定パーサ）を配置予定

セットアップ手順例:
1. 依存ライブラリを取得（例: `conan install . --output-folder=build` または `vcpkg install liblo nlohmann-json cli11 fmt spdlog` など）。
2. `cmake -S acoustics/pc_tools -B build -DCMAKE_BUILD_TYPE=Release`
3. `cmake --build build` で `agent_a_scheduler` / `agent_a_monitor` をビルド。
4. テスト実装後は `ctest --test-dir build` で `Catch2` ベースのテストを実行。

実行例:
- Scheduler: `./build/scheduler/agent_a_scheduler acoustics/pc_tools/scheduler/examples/basic_timeline.json --host 192.168.10.255 --port 9000 --bundle-spacing 0.02`
  - `--dry-run` で送信せず内容を確認、`--base-time 2024-05-01T21:00:00Z` でリードタイムの基準時刻を指定。
- Monitor: `./build/monitor/agent_a_monitor --port 9100 --csv logs/heartbeat.csv`
  - `--count` で受信パケット上限、`Ctrl+C` または `SIGINT` で停止。

各ディレクトリに `README.md` / `USAGE.md` を配置し、依存バージョンとコマンド例を併記すること。  
旧Python原型コードは段階的に削除予定のため、C++実装を追加したら不要なスクリプトを整理する。
