# Acoustics Runbook — 30 台 toio×M5 音響群を動かすまで

この Runbook は、リポジトリを取得した直後の状態から **30 台規模の toio 搭載 M5StickC Plus2 群を同時制御**するまでを、実コマンドとコード断片付きで説明する。既存の環境やログ構成を壊さないよう、`acoustics/docs/masterdocs.md` のポリシーを前提にしている。

---

## 1. 開発マシンの初期セットアップ

```bash
# 依存コンポーネント
sudo apt update && sudo apt install -y git python3 python3-venv cmake ninja-build
pipx install platformio                      # もしくは pip install --user platformio
npm install -g pnpm                          # Node 18+ 推奨。npm でも OK

# プロジェクト依存物をフェッチ
pnpm install                                 # ルート package.json が backend/dashboard をまとめて解決
cmake -S acoustics/pc_tools -B build/acoustics -G Ninja
cmake --build build/acoustics --target agent_a_scheduler agent_a_monitor
```

> 💡 `build/` 以下は生成物なので Git へは登録しない。CMake/Ninja のプリセットは `acoustics/pc_tools/README.md` を参照。

---

## 2. Secrets とサウンドアセット

1. **OSC 設定を作る**

```bash
cp acoustics/secrets/osc_config.example.json acoustics/secrets/osc_config.json
$EDITOR acoustics/secrets/osc_config.json    # Wi-Fi / OSC キー / 心拍送信先を埋める
python3 acoustics/tools/secrets/gen_headers.py
```

   - スクリプトが `acoustics/firmware/include/Secrets.h` を生成し、PlatformIO ビルドで使用される。
   - `Secrets.h` / `Secrets.myhome.h` は `.gitignore` 済み。鍵をログへ出力しないこと（Logging ポリシー参照）。

2. **サウンドアセットを SPIFFS に並べる**

```bash
ls acoustics/firmware/data/presets           # ここに WAV を配置
python3 acoustics/tools/sound_gen/generate_missing_f0.py --output acoustics/sound_assets/f0
```

   - `acoustics/firmware/data/manifest.json` と `acoustics/sound_assets/manifest.json` を同じ ID リストで保守する。
   - 大容量アセットは `acoustics/sound_assets/` 側を正とし、SPIFFS 用には必要分だけ同期する。

---

## 3. ファームウェアのビルドとデバイスへの書き込み

```bash
cd acoustics/firmware
pio run                              # ビルド
pio run -t upload                    # M5StickC Plus2 へ書き込み
pio run -t uploadfs                  # SPIFFS アセットを転送
pio device monitor -b 115200         # 起動ログを確認
```

Stick ごとに USB で接続して繰り返す。30 台まとめて書き込む場合は、番号付けした USB ハブを用意し、書き込み後に `mac` / `device_id` を `state/devices.json` に追記していくと後工程が楽になる。

---

## 4. デバイスレジストリとテレメトリ

### 4.1 Heartbeat モニタを常駐

```bash
mkdir -p acoustics/logs
./build/acoustics/monitor/agent_a_monitor \
  --port 19100 \
  --csv acoustics/logs/heartbeat_$(date +%Y%m%d).csv \
  --registry acoustics/state/devices.json
```

- `acoustics/state/devices.json` は実験用の一時置き場。正式な GUI/backend ではルート `state/devices.json` を読む。
- 収集が終わったら `jq` で整形して `state/devices.json` へマージ。

### 4.2 30 台 toio 用 ID 割り当て

1. `heartbeat.csv` に記録された `device_id` と `mac` を `state/devices.json` へ整理。
2. 物理 toio 車体に貼った番号と `device_id` を 1:1 で紐づけ、`acoustics/tests/test01/targets.json` のようなターゲットマップを 30 台分に拡張する。

```jsonc
{
  "cluster_a": ["dev-004b12c48530", "... (×10台)"],
  "cluster_b": ["dev-004b12c3c510", "... (×10台)"],
  "cluster_c": ["dev-004b12c49bb8", "... (×10台)"]
}
```

> 📌 大量台数では `--target-map` で論理グループを組み、GUI でも同じ JSON を参照できるようにする。

---

## 5. タイムライン作成とスケジューラ CLI

### 5.1 タイムライン生成

`acoustics/tools/timeline/generate_mother_mf_timeline.py` などのスクリプトでバンドル JSON を生成する。

```bash
python3 acoustics/tools/timeline/generate_mother_mf_timeline.py \
  --loops 2 \
  --tempo-scale 1.2 \
  --output acoustics/pc_tools/scheduler/examples/mother_mf_custom.json
```

必要なら自作タイムラインを `acoustics/tests` に配置し、Git で管理する。

### 5.2 CLI でドライラン → 本番送信

```bash
./build/acoustics/scheduler/agent_a_scheduler \
  acoustics/tests/test01/frog_round.json \
  --host 255.255.255.255 \
  --port 9000 \
  --bundle-spacing 0.05 \
  --lead-time 6 \
  --target-map acoustics/tests/test01/targets.json \
  --osc-config acoustics/secrets/osc_config.json \
  --dry-run
```

- ドライランで問題が無ければ `--dry-run` を外す。
- 30 台の場合、`--lead-time` は 6〜8 秒を推奨。ネットワーク遅延が大きい会場では 10 秒まで延ばす。
- `logs/gui_audit.jsonl` にも同じ送信履歴を書き出す場合は GUI backend を併用する。

---

## 6. GUI バックエンドとダッシュボード

### 6.1 バックエンド（Node/Express）

```bash
(cd acoustics/web/backend && pnpm install && pnpm run dev)
```

- デフォルト設定は `acoustics/web/backend/config.ts` の `defaults` を参照。
- ローカル設定を `config.json` に書く際はルート `state/` / `logs/` への相対パスを維持する。

API 例: タイムライン送信リクエスト

```bash
curl -X POST http://127.0.0.1:48100/api/timelines/dispatch \
  -H 'Content-Type: application/json' \
  -d '{
        "timelinePath": "acoustics/tests/test01/frog_round.json",
        "targetMap": "acoustics/tests/test01/targets.json",
        "leadTimeSeconds": 6,
        "dryRun": false
      }'
```

### 6.2 React/Vite ダッシュボード

```bash
(cd acoustics/web/dashboard && pnpm install && pnpm run dev)
```

GUI からできること:

1. **デバイス一覧**: `state/devices.json` を読み、オンライン台数・遅延統計を可視化。
2. **単発再生**: 任意プリセットとターゲットを選び `/acoustics/play` を即時送信。
3. **タイムライン送信**: ファイル選択＋ターゲットマップ指定で CLI 同等の送信を実行。
4. **ログ参照**: `logs/gui_audit.jsonl` / `logs/gui_dashboard_metrics.jsonl` をリアルタイムで tail 表示。

> GUI は最小権限で動かす。実験ログの保存先は `logs/` 直下に統一し、未来の解析を阻害しない。

### 6.3 WebSocket 経路の手動検証（テストスイート無し）

テスト用フレームワークではなく、本番と同じ WebSocket パイプラインで確認したい場合の手順。

1. **モニタソース（模擬 or 実機）を用意**

   - 実機がある場合は `agent_a_monitor --ws --ws-host 127.0.0.1 --ws-port 48080 --ws-path /ws/events`（監視プロセスが WebSocket を公開する構成）を使う。
   - 手元だけで流れを確認するならモックサーバー：

     ```bash
     pip install --user websockets
     python3 acoustics/tools/monitor/mock_ws_server.py \
       --devices state/devices.json \
       --host 127.0.0.1 --port 48080 --path /ws/events
     ```

     `state/devices.json` に登録済みの ID があれば、それらを使って擬似 heartbeat/diagnostics を流す。

2. **GUI backend を起動し、Monitor WS に接続**

   ```bash
   export GUI_BACKEND_CONFIG=acoustics/web/backend/config.json   # 省略可。monitorWsUrl が既定値なら不要
   (cd acoustics/web/backend && pnpm run dev)
   ```

   起動ログに `[MonitorClient] connected -> ws://127.0.0.1:48080/ws/events` が出れば、backend→monitor 間の WebSocket が成立。

3. **ダッシュボード無しで WebSocket push を観測**

   `ws` パッケージを使った最小 Node クライアント（`pnpm` が依存を解決済み）を走らせ、Push 内容をそのまま確認する。

   ```bash
   node <<'NODE'
   import WebSocket from "ws";
   const ws = new WebSocket("ws://127.0.0.1:48100");
   ws.on("message", (data) => {
     const msg = JSON.parse(data.toString());
     if (["heartbeat","diagnostics","receivelog","sendlog"].includes(msg.type)) {
       console.log(new Date().toISOString(), msg.type, msg);
     }
   });
   ws.on("open", () => console.log("[probe] connected to GUI hub"));
   ws.on("close", () => console.log("[probe] socket closed"));
   NODE
   ```

   - `type: "heartbeat"` が流れてくれば monitor → backend → hub の経路が成立している。
   - `timelineService.dispatch` など REST API を叩けば、`sendlog` イベントも同じクライアントで観測できる。

4. **GUI で最終確認**

   React ダッシュボードを起動し、デバイスリストがリアルタイムで更新されることを確認する。上記 Node クライアントを併用すると、GUI 表示と wire 上の実データを突き合わせられる。

### 6.4 Monitor → Backend → Dashboard → Scheduler の起動シーケンス

本番実験で毎回迷わないよう、必要なプロセスを順に並べる。各コマンドは別シェルで実行する。

1. **Monitor（心拍＋WebSocket サーバ）**
   ```bash
   mkdir -p acoustics/logs
   ./build/acoustics/monitor/agent_a_monitor \
     --port 19100 \
     --csv acoustics/logs/heartbeat_$(date +%Y%m%d).csv \
     --registry acoustics/state/devices.json \
     --ws --ws-host 127.0.0.1 --ws-port 48080 --ws-path /ws/events
   ```
   - `acoustics/state/devices.json` がリアルタイム更新される。検証後に `state/devices.json` へマージ。

2. **GUI backend**
   ```bash
   (cd acoustics/web/backend && pnpm run dev)
   ```
   - 起動ログに `[MonitorClient] connected -> ws://127.0.0.1:48080/ws/events` が出ることを確認。

3. **React Dashboard**
   ```bash
   (cd acoustics/web/dashboard && pnpm run dev)
   ```
   - ブラウザで `http://127.0.0.1:5173` を開き、デバイス一覧やログを確認。

4. **タイムライン送信（CLI or API）**
   ```bash
   ./build/acoustics/scheduler/agent_a_scheduler \
     acoustics/tests/test01/frog_round.json \
     --host 255.255.255.255 \
     --port 9000 \
     --bundle-spacing 0.05 \
     --lead-time 6 \
     --target-map acoustics/tests/test01/targets.json \
     --osc-config acoustics/secrets/osc_config.json
   ```
   - GUI から送る場合は `/api/timeline/send` を利用し、結果は `logs/gui_audit.jsonl` と WebSocket push (`sendlog`) に記録される。

5. **後処理**
   - `acoustics/logs/heartbeat_YYYYMMDD.csv` をアーカイブし、サマリのみ `logs/` 直下へコピー。
   - `acoustics/state/devices.json` の変更を確認し、ルート `state/devices.json` に反映。
   - 必要なら `logs/gui_audit.jsonl`、`logs/gui_dashboard_metrics.jsonl` を日付別ファイルにローテート。

---

## 7. 30 台 toio 運用フロー（例）

1. **物理準備**: toio プレイマットまたはマーカーシートを 3 クラスター（10 台ずつ）に区切る。各 toio に Stick モジュールを載せ、ID ラベルを確認。
2. **電源投入**: 10 台ずつ時差で給電し、`agent_a_monitor` が `last_seen` を記録するのを待つ。
3. **GUI で状態確認**: Cluster A/B/C の稼働台数、遅延などを GUI でチェック。異常台は再起動。
4. **タイムライン送信**: 
   ```bash
   ./build/acoustics/scheduler/agent_a_scheduler \
     acoustics/tests/mother_mf/mother_round.json \
     --target-map acoustics/tests/targets_30toio.json \
     --lead-time 8 --bundle-spacing 0.05 --osc-config acoustics/secrets/osc_config.json
   ```
5. **サウンド＆ロコモーション同期**: toio 制御システム（別リポジトリ想定）が `cluster_*` ID を参照して動作指令を出す。音響モジュールはタイムタグで同期済みなので、toio は `t0` で指定位置へ移動するだけでよい。
6. **ログ保全**: 実験終了後、`logs/gui_audit.jsonl`, `acoustics/logs/heartbeat_*.csv` を日付入りで保存し、`state/devices.json` をバックアップ。

---

## 8. よくあるトラブルとチェック

| 症状 | 代表的な原因 | 対処 |
| --- | --- | --- |
| Stick が Wi-Fi に繋がらない | `Secrets.h` の SSID/PASS が違う / 2.4GHz 未対応 AP | `acoustics/tools/secrets/gen_headers.py` を再実行し、2.4GHz SSID にする |
| タイムラインが遅れる | `--lead-time` 不足 / Monitor port 遮断 | `leadTimeSeconds` を +2s, `bundle-spacing` を 0.02〜0.05 に調整 |
| GUI から送れない | `schedulerBinary` パスが不正 / 権限不足 | `acoustics/web/backend/config.json` で `../../build/scheduler/agent_a_scheduler` に戻す |
| 30 台のうち一部だけ鳴る | `targets.json` に ID 漏れ / toio 側電源不足 | `state/devices.json` を元に自動生成するスクリプトを作り、Map を再構築 |

---

## 9. 次の一歩

- `acoustics/tests/` に 30 台スケール用のテンプレート (`targets_30.json`, `timeline_30.json`) を追加すると再現性が上がる。
- toio 側ロコモーションを自動化する場合は、音響タイムラインと toio パス計画を 1 つの JSON にまとめ、GUI から同時配信する設計を検討する。

この Runbook を起点に、セットアップ～大量運用の流れを関係者へ共有し、状態ファイル／ログの所在を揃えていくことが安定運用への近道となる。
