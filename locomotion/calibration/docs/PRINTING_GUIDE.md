# キャリブレーション用マーカー印刷ガイド

キャリブレーション用のChArUcoボードを印刷するための手順です。

## 印刷すべきもの

### 1. ChArUcoボード（必須）

キャリブレーションには**ChArUcoボード1枚**を印刷します。

- **サイズ**: 180mm × 270mm（5x7グリッド、45mmマスの場合）
- **内容**: チェス盤パターン + ArUcoマーカー
- **用途**: カメラの歪み補正と画像→床面の変換
- **用紙**: A3用紙1枚に収まります（297mm × 420mm）
- **重要**: **4枚のPDFを並べる必要はありません。1枚のPDFを1枚の用紙に印刷してください。**

**よくある誤解**: 「四点計測」という言葉から、4枚のPDFを印刷する必要があると思われるかもしれませんが、実際には1枚のChArUcoボードの4つの角（左上、右上、左下、右下）のtoio Position ID座標を計測するだけです。

#### 大きなプレイマットエリア（1.2m×1.2mなど）の場合

もし複数のA3プレイマットを並べて大きなエリア（例：1.2m×1.2m）を使用する場合：

- **ChArUcoボードは1枚で十分です**
  - ホモグラフィは平面の変換なので、1枚のボードで平面全体の変換を計算できます
  - ボードはカメラの視野範囲内にあればよい
  - ボードの位置は、計測したいエリア内のどこでも構いません（通常は中央に配置）

- **12枚のPDFを印刷する必要はありません**
  - ChArUcoボードは1枚のPDFを1枚の用紙（A3）に印刷
  - プレイマットが12枚あっても、ChArUcoボードは1枚で十分

- **ボードの配置**
  - 1.2m×1.2mのエリア全体をカバーする必要はありません
  - ボード（180mm×270mm）をエリア内の適切な位置（例：中央）に配置
  - カメラがボードを認識できれば、そのホモグラフィでエリア全体の座標変換が可能

**注意**: カメラの視野が1.2m×1.2m全体をカバーできない場合、カメラの位置やレンズの選択を検討する必要があります。ChArUcoボード自体を大きくする必要は通常ありません。

### 2. 印刷ガイドPDF（推奨）

印刷手順とボード仕様を含むPDFを生成できます。

## 印刷ガイドPDFの生成

### 方法1: 完全版（推奨）

`reportlab`を使用して、ボード画像と印刷手順を含む完全なPDFを生成します。

#### インストール方法

**オプションA: 仮想環境を使用（推奨）**

```bash
# 仮想環境を作成
python3 -m venv venv

# 仮想環境をアクティベート
source venv/bin/activate

# reportlabをインストール
pip install reportlab

# PDFを生成
python3 locomotion/calibration/tools/generate_printing_guide.py \
  --output charuco_board_printing_guide.pdf
```

**オプションB: ユーザーインストール**

```bash
# reportlabをユーザーディレクトリにインストール
pip3 install --user reportlab

# PDFを生成
python3 locomotion/calibration/tools/generate_printing_guide.py \
  --output charuco_board_printing_guide.pdf
```

**オプションC: Homebrewでインストール**

```bash
# reportlabをHomebrewでインストール（利用可能な場合）
brew install reportlab
```

このPDFには以下が含まれます：
- ChArUcoボード画像
- ボード仕様
- 印刷手順
- 注意事項

### 方法2: シンプル版（最も簡単）

`Pillow`のみを使用して、ボード画像のみのPDFを生成します。
**reportlabがインストールできない場合でも、この方法で動作します。**

#### インストール方法

**オプションA: 仮想環境を使用**

```bash
# 仮想環境を作成
python3 -m venv venv

# 仮想環境をアクティベート
source venv/bin/activate

# Pillowをインストール
pip install Pillow

# PDFを生成（画像のみ）
python3 locomotion/calibration/tools/generate_printing_guide.py \
  --output charuco_board_printing_guide.pdf
```

**オプションB: ユーザーインストール**

```bash
# Pillowをユーザーディレクトリにインストール
pip3 install --user Pillow

# PDFを生成（画像のみ）
python3 locomotion/calibration/tools/generate_printing_guide.py \
  --output charuco_board_printing_guide.pdf
```

**注意**: この方法では、PDFにはChArUcoボード画像のみが含まれ、印刷手順や注意事項は含まれません。
印刷手順は、このドキュメント（`PRINTING_GUIDE.md`）を参照してください。

### 方法3: 既存のボード画像を使用

既に生成済みのボード画像がある場合：

```bash
python3 locomotion/calibration/tools/generate_printing_guide.py \
  --board-image charuco_board_5x7_45mm.png \
  --output charuco_board_printing_guide.pdf
```

## 手動での印刷手順

PDFを生成しない場合でも、以下の手順で印刷できます。

### 1. ChArUcoボード画像を生成

**前提条件**: OpenCVが必要です。仮想環境にインストールしてください。

```bash
# 仮想環境をアクティベート
source venv/bin/activate

# OpenCVをインストール
pip install opencv-python

# ChArUcoボード画像を生成
python3 locomotion/calibration/tools/generate_charuco_board.py \
  --output charuco_board_5x7_45mm.png \
  --metadata charuco_board_5x7_45mm.json
```

**注意**: `opencv-python`のインストールに失敗する場合（numpyビルドエラーなど）、`--only-binary :all:`オプションを使用するか、事前にnumpyをインストールしてください。

### 2. 印刷設定

1. **用紙サイズ**: **A3用紙1枚**（297mm × 420mm）に収まります
   - ボードサイズ: 180mm × 270mm + 余白（10mm × 2）= 200mm × 290mm
   - A3用紙（297mm × 420mm）なら十分に収まります
   - **重要**: 4枚のPDFを並べる必要はありません。1枚のPDFを1枚の用紙に印刷してください。

2. **印刷品質**: 最高品質を選択

3. **カラー**: 白黒またはグレースケールで十分

4. **スケール**: **「実際のサイズ」「100%」「余白なし印刷」**を選択
   - ⚠️ **重要**: スケールを変更すると実寸が合わなくなります
   - ⚠️ **重要**: 「ページに合わせる」「用紙サイズに合わせる」などの自動調整機能を無効化してください

### 3. 実寸確認

印刷後、必ず定規で実寸を確認してください：

- **チェス盤マス**: 45mm × 45mm
- **ArUcoマーカー**: 33mm × 33mm
- **ボード全体**: 180mm × 270mm（余白除く）

### 4. 調整が必要な場合

実寸が合わない場合は、スクリプトのパラメータを調整して再生成：

```bash
# 例: マスサイズを46mmに調整
python3 locomotion/calibration/tools/generate_charuco_board.py \
  --square-length-mm 46.0 \
  --output charuco_board_5x7_46mm.png
```

## 印刷後の作業

1. **ボードの固定**: プレイマット上に固定（テープなどで）
   - 1枚のボードをプレイマットの適切な位置（例：中央）に配置
   - ボードが平面で固定されていることを確認（歪みや折れがないこと）
   - **大きなエリア（1.2m×1.2mなど）の場合**: エリア全体をカバーする必要はありません。ボードはカメラの視野範囲内にあればよい

2. **4つの角の計測**: toio Position ID座標を計測
   - ボードの4つの角（左上、右上、左下、右下）のtoio Position ID座標を計測
   - **重要**: これは「4枚のPDF」や「12枚のPDF」ではなく、「1枚のボードの4つの角」の座標です
   - toioキューブを各角に配置してPosition IDを読み取る
   - **大きなエリアの場合**: ボードの位置がエリアのどこにあっても構いませんが、計測する4つの角のPosition ID座標を正確に記録してください

3. **設定ファイル更新**: `config/toio_playmat.json` に計測値を記録
   - ボードの4つの角のtoio Position ID座標を `correspondences` に追加
   - プレイマットのサイズが大きい場合（複数のA3プレイマットを並べた場合など）は、`playmat_id` や `physical_size_mm` を適切に更新

詳細は `docs/four_point_measurement.md` を参照してください。

## トラブルシューティング

### 実寸が合わない

- プリンタのスケール設定を確認（100%になっているか）
- 用紙サイズの設定を確認
- プリンタの「ページに合わせる」などの自動調整機能を無効化

### 画像がぼやける

- 印刷品質を「最高」に設定
- 解像度を上げて再生成（`--dpi 600`など）

### PDFが生成できない

- `reportlab`または`Pillow`がインストールされているか確認
- エラーメッセージを確認して必要なライブラリをインストール

### OpenCVがインストールできない（cv2モジュールエラー）

ChArUcoボード画像を生成するには、OpenCVが必要です。

**エラーメッセージ**:
```
ModuleNotFoundError: No module named 'cv2'
```

**解決方法**:

1. **バイナリ版をインストール（推奨）**
   ```bash
   pip install --only-binary :all: opencv-python
   ```

2. **numpyを事前にインストール**
   ```bash
   pip install numpy
   pip install opencv-python
   ```

3. **Homebrewでインストール（macOS）**
   ```bash
   brew install opencv
   ```

### 4枚のPDFを並べる必要があるか？

**いいえ、必要ありません。** ChArUcoボードは1枚のPDFを1枚の用紙（A3推奨）に印刷します。

- ボードサイズ: 180mm × 270mm（余白含めると200mm × 290mm）
- A3用紙: 297mm × 420mm（十分に収まります）
- 「四点計測」は、1枚のボードの4つの角の座標を計測することを意味します

### 1.2m×1.2mのエリアで12枚のPDFを印刷する必要があるか？

**いいえ、必要ありません。** 大きなプレイマットエリア（1.2m×1.2mなど）でも、ChArUcoボードは1枚で十分です。

- **ChArUcoボードの役割**: カメラの視野範囲内の平面の変換を計算するための参照点
- **ホモグラフィの性質**: 平面の変換なので、1枚のボードで平面全体の変換を計算できます
- **ボードの配置**: エリア内の適切な位置（例：中央）に1枚のボードを配置するだけで十分
- **プレイマットが12枚あっても**: ChArUcoボードは1枚のPDFを1枚の用紙に印刷すればOK

**重要なポイント**:
- ChArUcoボードはプレイマット全体を覆う必要はありません
- ボードはカメラの視野範囲内にあればよい
- 1枚のボードで、カメラが視野に収める範囲全体の座標変換が可能です

### インストールエラー（macOS）

macOSでは、システムのPythonが外部管理されているため、以下のエラーが発生する場合があります：

```
error: externally-managed-environment
```

**解決方法:**

1. **仮想環境を使用（推奨）**
   ```bash
   python3 -m venv venv
   source venv/bin/activate
   pip install reportlab  # または Pillow
   ```

2. **ユーザーインストール**
   ```bash
   pip3 install --user reportlab  # または Pillow
   ```

3. **numpyビルドエラーが発生する場合**
   - `reportlab`の代わりに`Pillow`のみを使用（シンプル版）
   - または、事前にnumpyをインストール: `pip3 install --user numpy`
   - または、Homebrewでnumpyをインストール: `brew install numpy`

### reportlabがインストールできない場合

`Pillow`のみを使用することで、PDFを生成できます（画像のみ）。
印刷手順は、このドキュメント（`PRINTING_GUIDE.md`）を参照してください。

## 参考

- `tools/generate_charuco_board.py` - ChArUcoボード生成スクリプト
- `tools/generate_printing_guide.py` - 印刷ガイドPDF生成スクリプト
- `docs/four_point_measurement.md` - 四点計測手順

