# キャリブレーション実装要件定義書

**対象モジュール:** `locomotion/calibration`  
**言語 / 標準:** C++20（libc++/libstdc++ いずれも許容）  
**依存:** librealsense2, OpenCV（aruco + core + imgproc + calib3d）, spdlog,（任意）Eigen3  
**目的:** toio プレイマット上の絶対座標系に RealSense センサ出力を正規化するためのオンライン・オフライン併用キャリブレーション機能を提供する。

---

## 1. スコープと前提
- **入力:** RealSense D435 から取得するカラー（BGR8）/深度（Z16）フレーム。  
- **出力:**  
  - toio プレイマットの Position ID 座標系への 3×3 ホモグラフィ行列 `H_color_to_position`。  
  - 床平面方程式 \(ax + by + cz + d = 0\)。  
  - 再投影誤差および検証メトリクス。  
- **利用場面:** デプロイ初期のオフラインキャリブレーション、および運用中の軽量リファイン。  
- **非対象:** カメラ内パラ推定、BLE 通信、toio 制御ロジック。

---

## 2. アーキテクチャ概要
### 2.1 モジュール構成
```
locomotion/calibration/
 ├─ include/locomotion/calibration/CalibrationPipeline.h
 ├─ src/CalibrationPipeline.cpp
 ├─ src/FloorPlaneEstimator.h/.cpp          (新規追加予定)
 ├─ src/CharucoDetector.h/.cpp              (共通化予定)
 ├─ src/CalibrationSession.h/.cpp           (I/O + ロギング制御)
 ├─ config/                                 (JSON サンプル配置)
 └─ tools/
     └─ capture_calibration.cpp             (CLI ユーティリティ)
```

### 2.2 主要クラス責務
- **CalibrationPipeline**  
  - RealSense パイプライン管理（ストリーム設定、フレーム取得、リソース解放）。  
  - 各処理ステップを連結し、`CalibrationSnapshot` を生成する。
- **CharucoDetector（新設）**  
  - ChArUco 検出・補間・ID 整理を担当。  
  - マーカー未検出時のリトライ／閾値調整 API を提供。
- **FloorPlaneEstimator（新設）**  
  - 深度点群から RANSAC で床平面を推定し、法線方向の安定化を行う。  
  - 併せて外れ値比率やサンプル数を算出。
- **CalibrationSession（新設）**  
  - 繰り返し実行／結果の統計集計・閾値判定。  
  - 結果のシリアライズ（JSON/CBOR）とログ出力。

---

## 3. 実装詳細要件
### 3.1 RealSense 入力
- `rs2::config` でカラー/深度を有効化し、`rs2::pipeline::start` で開始する。  
- フレーム待受は `wait_for_frames`（タイムアウト 500 ms）を基本とし、`RS2_EXCEPTION_TYPE_PIPELINE_BUSY` を受けた場合の再試行を実装する。  
- `rs2::align`（カラー基準）を再利用し、カラーと深度の解像度が一致することを assert する。  
- `FrameBundle` はメモリコピー（`clone()`）でバッファ破棄の副作用を避ける。

### 3.2 ChArUco 検出
- `CharucoDetector::Detect(const cv::Mat& color)` は以下を返すこと:
  - サブピクセル補正済み 2D 点列（`std::vector<cv::Point2f>`）。  
  - ボード座標系の 3D 点列（`std::vector<cv::Point3f>`）。  
  - 有効マーカー枚数、補間成功率などの統計情報。
- 検出パラメータは `CalibrationConfig` の `charuco_enable_subpixel_refine` / `charuco_subpixel_*` で外部指定可能にする。
- 有効頂点数が設定閾値（既定 12）未満の場合は `std::nullopt` を返す。
- 検出に失敗した場合はログ（Info レベル）に記録し、キャリブレーションループが続行できるようにする。
- toio Position ID への変換に必要なマット基準点（原点座標、X/Y 方向ベクトル、ID スケール）は `CalibrationConfig::playmat_layout_path` が指す外部ファイル（例: `config/toio_playmat.json`）から読み込む。
- レイアウト JSON には以下を含める:  
  - `playmats[i].position_id_extent.min/max`: PDF 記載の Start/End ID  
  - `playmats[i].id_per_mm`: 公称 ID↔mm 換算（実測で更新）  
  - `charuco_mounts[j].board_to_position_id.correspondences`: Charuco ボード座標（mm）と Position ID の対応点（最低 3 点、推奨 4 点以上）
- レイアウト情報を扱う `PlaymatLayout` コンポーネントを実装し、以下を提供する:  
  - JSON 読み込みとバリデーション。  
  - Charuco ボード座標（mm）→ Position ID のアフィン変換生成（最小二乗）。  
  - 複数マット構成（`playmats` 配列）と将来のマット間オフセット対応。  
  - `CalibrationPipeline` から利用可能な API:  
    ```cpp
    PlaymatLayout layout = PlaymatLayout::Load(config.playmat_layout_path);
    cv::Point2f playmat_pt = layout.TransformBoardPoint("a3_simple", "center_mount_nominal", board_pt_mm);
    ```
- `CalibrationConfig::board_mount_label` で使用する対応関係を指定し、異なる Charuco 設置場所を柔軟に扱えるようにする。
- 開発・検証用の ArUco シートは `tools/generate_marker_sheet.py` で生成し、45 mm 角マーカー（A4／300 dpi）を既定テンプレートとする。

### 3.3 ホモグラフィ推定
- `cv::findHomography` を RANSAC モードで呼び出し、閾値は Pixel 空間で `CalibrationConfig::homography_ransac_thresh_px` を使用する。  
- 変換先は toio Position ID 座標系（例: PDF 参照の開始点 `Start (x=98, y=142)`、終了点 `End (x=402, y=358)`）とし、マットごとのオフセット・回転を考慮した補正行列を乗算して `H_color_to_position` を得る。  
- `CalibrationSnapshot::reprojection_error` は Position ID 座標系の平均二乗平方根（RMS）として算出する。  
- `CalibrationSession` は複数スナップショットから中央値／分散を評価し、`max_reprojection_error_id` 以下であることを保証する。
- （現状実装メモ）Playmat レイアウトによる補正は未着手のため、暫定的に Charuco ボード座標（mm）をそのまま Position ID 座標として扱う。レイアウト JSON が整い次第、射影前に補正マトリクスを乗算する。

### 3.4 床平面推定
- `FloorPlaneEstimator` は以下のパラメータを持つ:  
  - サンプリング解像度（例: 4×4 グリッド）  
  - RANSAC 反復回数（既定 500）  
  - インライヤー閾値（mm 単位で指定）
- 深度→点群変換には RealSense の `rs2_intrinsics` を使用。  
- 推定後、法線の向きがカメラを向くように符号調整し、平面距離の標準偏差を算出して `CalibrationSnapshot` へ付与する。

### 3.5 セッション管理と永続化
- `CalibrationSession` は以下の責務を持つ:
  - `Run()` で `N` 回（設定、既定 5）スナップショットを取得し、統計評価を実施。
  - `CalibrationSnapshot` の配列から最良値を選び、`CalibrationResult`（RMS、インライヤー率など）を整形する。
  - JSON 永続化・ログ出力を担当し、成功・失敗を呼び出し元へ返す。
- ベスト採用条件:
  - 再投影誤差 < `max_reprojection_error_id`。  
  - 平面の標準偏差 < `max_plane_std_mm`（設定）。  
  - タイムスタンプが最新。
- 永続化フォーマット:  
  ```json
  {
    "timestamp": "...",
    "homography_color_to_position": [[...],[...],[...]],
    "floor_plane": [a,b,c,d],
    "reprojection_error_id": 3.2,
    "inlier_ratio": 0.94,
    "charuco_points": 42
  }
  ```
- コンフィグとの整合性を検証し、バージョン（`schema_version`）を埋め込む。
- CLI ユーティリティ `capture_calibration` を提供し、以下のワークフローを実装する:
  1. JSON/CLI 引数から `CalibrationConfig` を構築。
  2. `CalibrationSession` を起動し、結果を `calib_result.json` などへ保存。
  3. 失敗時はリトライ回数・理由をログ出力し、必要なら非ゼロ終了コードを返す。

### 3.6 ロギング・エラーハンドリング
- `spdlog` レベル設定（Info/Debug/Warning/Error）を `CalibrationConfig::log_level`（enum）で指定。  
- RealSense 例外はキャッチしてリトライ回数を制限（3 回）。  
- ホモグラフィ推定失敗時は調整済みパラメータをログに吐き、デバッグ用の画像ダンプ（`tools/debug/`）をオプションで出力。

### 3.7 テスト要件
- **ユニットテスト:**  
  - ChArUco 検出の入力にサンプル画像（モック）を与え、想定数のコーナーが得られるか確認。  
  - RANSAC 平面推定が既知平面を再構築できるか検証。
- **統合テスト:**  
  - RealSense が接続されていなくてもコンフィグ検証とエラー処理が動作する。  
  - 実機接続時に `CalibrationSession` が成功するまでの時間計測。
- **CI 連携:** ハード依存部分は `REAL_DEVICE_TESTS` フラグで分岐。デフォルトではモックデータを使用。

### 3.8 パフォーマンス要件
- キャリブレーション 1 回の処理時間 ≤ 150 ms（カラー 1280×720）。  
- メモリ確保は極力初期化時に行い、ループ内では再利用する。  
- RANSAC で利用する乱数生成は `std::mt19937` を共有し、シードを `CalibrationConfig::random_seed` で制御可能に。

---

## 4. 実装ステップ推奨順
1. `CharucoDetector` クラスの切り出しとユニットテスト整備。  
2. `FloorPlaneEstimator` の RANSAC 実装とベンチマーク。  
3. `CalibrationPipeline` をこれらヘルパーに委譲し、構成要素を疎結合化。  
4. `CalibrationSession` で反復実行・統計評価を追加。  
5. CLI ツール `capture_calibration` を実装し、設定読み込み〜結果保存を一連で動かす。  
6. CI でモックテストを実行し、実機用テストは別ジョブで手動トリガー。

---

## 5. 保守・将来拡張
- プロジェクタ連携を想定し、`CalibrationSnapshot` にカメラ外部パラメータ（`R`, `t`）を追加できるよう空欄を確保。  
- BLE 制御側と共有するため、`calib.json` のスキーマを OpenAPI/JSON Schema で整備。  
- 長期運用でのドリフト補正のため、既知 toio をアンカーとして `CalibrationSession` で利用できるホットリロード API を準備。

---

## 付録 A: コンフィグ項目一覧（抜粋）

| キー | 型 | 既定値 | 説明 |
|------|----|--------|------|
| `color_width` | int | 1280 | カラー画像幅 |
| `color_height` | int | 720 | カラー画像高さ |
| `depth_width` | int | 848 | 深度画像幅 |
| `depth_height` | int | 480 | 深度画像高さ |
| `fps` | int | 30 | ストリーム FPS |
| `charuco_squares_x` | int | 5 | ChArUco ボード列数 |
| `charuco_squares_y` | int | 7 | ChArUco ボード行数 |
| `charuco_square_length_mm` | float | 45.0 | チェス盤マス一辺長 |
| `charuco_marker_length_mm` | float | 33.0 | マーカー一辺長 |
| `min_charuco_corners` | int | 12 | ホモグラフィ最小採用点数 |
| `homography_ransac_thresh_px` | double | 3.0 | `findHomography` のしきい値（px） |
| `max_reprojection_error_id` | double | 8.0 | Position ID 単位の許容誤差 |
| `charuco_enable_subpixel_refine` | bool | true | Charuco 検出時のサブピクセル補正を有効化 |
| `charuco_subpixel_window` | int | 5 | サブピクセル補正ウィンドウ（`Size(w,w)`） |
| `charuco_subpixel_max_iterations` | int | 30 | サブピクセル補正の最大反復回数 |
| `charuco_subpixel_epsilon` | double | 0.1 | サブピクセル補正の収束閾値 |
| `enable_floor_plane_fit` | bool | true | 床平面推定スイッチ |
| `floor_inlier_threshold_mm` | double | 8.0 | 平面インライヤー閾値 |
| `floor_ransac_iterations` | int | 500 | RANSAC 反復数 |
| `session_attempts` | int | 5 | キャリブレーション試行回数 |
| `random_seed` | uint64 | 42 | 乱数シード |
| `log_level` | string | `"info"` | spdlog ログレベル |
| `aruco_dictionary` | string | `"DICT_4X4_50"` | OpenCV ArUco 辞書名 |
| `playmat_layout_path` | string | `"config/toio_playmat.json"` | Position ID レイアウト定義ファイル |
| `board_mount_label` | string | `"center_mount_nominal"` | レイアウト JSON 内の Charuco マウント識別子 |

---

## 付録 B: 参考文献リンク
- librealsense Align サンプル  
  https://github.com/IntelRealSense/librealsense/tree/master/examples/align  
- OpenCV Charuco サンプル  
  https://github.com/opencv/opencv/blob/master/samples/cpp/tutorial_code/calib3d/camera_calibration/charuco_diamond.cpp  
- RANSAC 平面フィット参考  
  https://pointclouds.org/documentation/tutorials/random_sample_consensus.php
