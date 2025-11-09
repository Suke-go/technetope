# マーカー生成ツール

このディレクトリには、キャリブレーション用のマーカーを生成する2つのスクリプトが含まれています。

## 1. ChArUcoボード生成 (`generate_charuco_board.py`)

キャリブレーション用のChArUcoボード（チェス盤 + ArUcoマーカー）を生成します。
**こちらがキャリブレーション用の推奨ツールです。**

### 使い方

```bash
python locomotion/calibration/tools/generate_charuco_board.py \
  --output assets/boards/charuco_board_5x7_45mm.png \
  --metadata assets/boards/charuco_board_5x7_45mm.json
```

### パラメータ

- `--squares-x`: 横方向のチェス盤マス数（既定 5）
- `--squares-y`: 縦方向のチェス盤マス数（既定 7）
- `--square-length-mm`: チェス盤マス一辺の長さ [mm]（既定 45mm）
- `--marker-length-mm`: ArUcoマーカー一辺の長さ [mm]（既定 33mm）
- `--dictionary`: ArUco辞書名（既定 DICT_4X4_50）
- `--dpi`: 出力解像度（既定 300）
- `--margin-mm`: ボード周囲の余白 [mm]（既定 10mm）

### 印刷時の注意

1. プリンタ設定で「実際のサイズ」「100%」「余白なし印刷」を選択
2. チェス盤マスが実寸 45mm か定規で確認
3. ArUcoマーカーが実寸 33mm か定規で確認
4. 必要に応じてパラメータを調整して再生成

### 参考

- 5x7グリッドの場合、ボードサイズは 180mm × 270mm（(5-1)×45mm × (7-1)×45mm）
- このサイズは toio プレイマット（A3）の中央に配置するのに適しています
- RealSense D415 を 60–80cm 上方に設置した場合でも十分な解像度が確保できます

## 2. 印刷ガイドPDF生成 (`generate_printing_guide.py`)

ChArUcoボード画像と印刷手順を含むPDFを生成します。
印刷用の資料として使用できます。

### 使い方

```bash
# 仮想環境を使用（推奨）
python3 -m venv venv
source venv/bin/activate
pip install reportlab  # または Pillow（シンプル版）
python3 locomotion/calibration/tools/generate_printing_guide.py \
  --output charuco_board_printing_guide.pdf

# ユーザーインストール
pip3 install --user reportlab  # または Pillow（シンプル版）
python3 locomotion/calibration/tools/generate_printing_guide.py \
  --output charuco_board_printing_guide.pdf
```

**注意**: macOSでは、システムのPythonが外部管理されているため、仮想環境または`--user`フラグの使用を推奨します。
詳細は `docs/PRINTING_GUIDE.md` を参照してください。

### パラメータ

- `--output`: 出力PDFファイルパス
- `--board-image`: 既存のボード画像を使用する場合
- その他のパラメータは `generate_charuco_board.py` と同じ

### 出力内容

- ChArUcoボード画像
- ボード仕様
- 印刷手順
- 注意事項

詳細は `docs/PRINTING_GUIDE.md` を参照してください。

## 3. ArUcoマーカーシート生成 (`generate_marker_sheet.py`)

個別のArUcoマーカーをA4用紙向けにレイアウトしたテンプレート画像を生成します。
ChArUcoボードではなく、個別のArUcoマーカーが必要な場合に使用します。

### 使い方

```bash
python3 locomotion/calibration/tools/generate_marker_sheet.py \
  --output assets/markers/markers_a4_45mm.png \
  --metadata assets/markers/markers_a4_45mm.json
```

### パラメータ

- `--marker-size-mm`: マーカーの一辺（既定 45mm）
- `--border-mm`: マーカー周囲の白枠（既定 3mm）
- `--rows / --columns`: 用紙上に並べる行数・列数
- `--start-id`: 任意の ID から連番で生成
- `--individual-dir`: 指定すると各マーカーを個別 PNG として保存
- `--dictionary`: OpenCV の `cv::aruco::PREDEFINED_DICTIONARY_NAME`

### 参考

- 45 mm 角は toio プレイマットの約 1.3 マス幅で、キューブが重ならず扱いやすいサイズです
- RealSense D435 を 60–80 cm 上方に設置した場合でも 30–50 px 程度の解像度が確保できます
- マーカー ID と配置位置は `--metadata` で JSON 保存できるため、キャリブレーションの自動検証にも転用できます
