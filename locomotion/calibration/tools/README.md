# マーカー生成ツール

`generate_marker_sheet.py` は OpenCV の ArUco マーカーを A4 用紙向けにレイアウトしたテンプレート画像を生成するスクリプトです。既定では 300 dpi、45 mm 角のマーカーを 2 列 × 4 行で配置し、プリンタでスケール 100% のまま印刷すれば toio プレイマット上で使いやすいサイズになります。OpenCV 4.10 以降で追加された `generateImageMarker` API と従来の `drawMarker` の両方に自動対応します。

## 使い方

```bash
python locomotion/calibration/tools/generate_marker_sheet.py \
  --output markers_a4_45mm.png \
  --metadata markers_a4_45mm.json
```

- `--marker-size-mm`: マーカーの一辺（既定 45mm）  
- `--border-mm`: マーカー周囲の白枠（既定 3mm）  
- `--rows / --columns`: 用紙上に並べる行数・列数  
- `--start-id`: 任意の ID から連番で生成  
- `--individual-dir`: 指定すると各マーカーを個別 PNG として保存  
- `--dictionary`: OpenCV の `cv::aruco::PREDEFINED_DICTIONARY_NAME`

印刷時は以下を推奨します。

1. 出力画像をプレビューし、用紙設定を A4/300 dpi に合わせる。  
2. プリンタ設定で「実際のサイズ」「100%」「余白なし印刷」等を選択。  
3. 45 mm の正方形が実寸通りか定規で確認し、必要に応じて `--marker-size-mm` を微調整。

## 参考

- 45 mm 角は toio プレイマットの約 1.3 マス幅で、キューブが重ならず扱いやすいサイズです。  
- RealSense D435 を 60–80 cm 上方に設置した場合でも 30–50 px 程度の解像度が確保できます。  
- マーカー ID と配置位置は `--metadata` で JSON 保存できるため、キャリブレーションの自動検証にも転用できます。
