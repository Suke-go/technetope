# ChArUcoボードの4つの角の計測方法の説明

## 質問: 端っこの計算はpoint cloudとかで実行するってこと？

**答え: いいえ、point cloudでは実行しません。手動で計測します。**

## 仕組みの説明

### 1. 4つの角のtoio Position ID座標は**手動で計測**

ChArUcoボードの4つの角（左上、右上、左下、右下）のtoio Position ID座標は、**手動で計測**します。

#### 計測方法

1. **toioキューブを使用（推奨）**
   - ChArUcoボードをプレイマット上に配置（固定）
   - toioキューブをChArUcoボードの各角の位置に配置
   - toio SDKまたはアプリでPosition IDを読み取る
   - 各角のPosition ID座標を記録

2. **計測結果をJSONファイルに記録**
   - `config/toio_playmat.json`の`correspondences`に記録
   - 例：
     ```json
     "correspondences": [
       {
         "board_point_mm": { "x": 0.0, "y": 0.0 },
         "position_id": { "x": 121.14, "y": 44.77 }
       },
       {
         "board_point_mm": { "x": 180.0, "y": 0.0 },
         "position_id": { "x": 251.86, "y": 44.77 }
       },
       // ... 残りの2点
     ]
     ```

### 2. アフィン変換の自動計算

4つの角の対応点が記録されると、システムが**自動的にアフィン変換を計算**します。

#### 処理の流れ

1. **JSONファイルから対応点を読み込み**
   - `PlaymatLayout::LoadFromJson()`で`correspondences`を読み込み
   - ボード座標（mm）とtoio Position ID座標の対応点を取得

2. **アフィン変換行列を計算**
   - `computeAffine()`関数で、対応点からアフィン変換行列を計算
   - この変換行列で、ボード座標（mm）→ toio Position ID座標（ID units）への変換が可能

3. **任意の点の座標変換**
   - ボード上の任意の点（中心座標など）は、このアフィン変換で自動的にtoio Position ID座標に変換される
   - 手動で計測する必要はない

### 3. Point Cloudの役割

Point cloud（深度画像から生成される3D点群）は、以下の目的で使用されます：

1. **床面の推定**
   - `FloorPlaneEstimator`で、深度画像から床面の法線ベクトルを推定
   - RANSACアルゴリズムを使用して床面をフィッティング

2. **ホモグラフィ計算の補助**
   - 床面の法線ベクトルを使って、ボード座標を床面座標に投影
   - ただし、4つの角のtoio Position ID座標の計測には使用されない

### 4. 中心座標の設定

**中心座標は手動で設定する必要はありません。** アフィン変換によって自動的に計算されます。

#### 処理の流れ

1. **4つの角の対応点からアフィン変換を計算**
   ```cpp
   mount.affine_mm_to_position = computeAffine(source_mm, target_id);
   ```

2. **任意のボード座標をtoio Position ID座標に変換**
   ```cpp
   cv::Vec3d src(board_x_mm, board_y_mm, 1.0);
   cv::Vec3d dst = mount.affine_mm_to_position * src;
   // dst[0], dst[1]がtoio Position ID座標
   ```

3. **中心座標の例**
   - ボードの中心座標: `(90.0, 135.0)` mm（ボードサイズ180mm×270mmの場合）
   - この座標は、アフィン変換で自動的にtoio Position ID座標に変換される
   - 手動で計測する必要はない

## まとめ

### 手動で計測するもの

- ✅ **4つの角のtoio Position ID座標**（toioキューブを使用）

### 自動的に計算されるもの

- ✅ **アフィン変換行列**（4つの角の対応点から計算）
- ✅ **中心座標のtoio Position ID座標**（アフィン変換で自動計算）
- ✅ **ボード上の任意の点のtoio Position ID座標**（アフィン変換で自動計算）

### Point Cloudの役割

- ✅ **床面の推定**（深度画像から床面の法線ベクトルを推定）
- ❌ **4つの角のtoio Position ID座標の計測**（使用しない）

## 参考

- `docs/four_point_measurement.md` - 四点計測の詳細な手順
- `config/toio_playmat.json` - 対応点の設定ファイル
- `src/PlaymatLayout.cpp` - アフィン変換の計算実装
- `src/CalibrationPipeline.cpp` - ホモグラフィ計算と座標変換の実装

