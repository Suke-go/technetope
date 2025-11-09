#!/usr/bin/env python3
"""
generate_printing_guide.py
===========================

キャリブレーション用ChArUcoボードの印刷ガイドPDFを生成します。
ChArUcoボード画像と印刷手順を含むPDFを作成します。
"""

from __future__ import annotations

import argparse
import pathlib
import sys
from datetime import datetime

try:
    from reportlab.lib.pagesizes import A4, A3
    from reportlab.lib.units import mm
    from reportlab.pdfgen import canvas
    from reportlab.lib.utils import ImageReader
    HAS_REPORTLAB = True
except ImportError:
    HAS_REPORTLAB = False
    print("[WARN] reportlab is not installed. Install with: pip install reportlab")
    print("[INFO] Falling back to simple PDF generation using PIL")

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    print("[WARN] PIL (Pillow) is not installed. Install with: pip install Pillow")


def generate_charuco_board_image(output_path: pathlib.Path, **kwargs) -> pathlib.Path:
    """ChArUcoボード画像を生成"""
    import subprocess
    
    # OpenCVがインストールされているか確認
    try:
        import cv2
    except ImportError:
        raise RuntimeError(
            "OpenCV (cv2) is not installed. Please install it with:\n"
            "  pip install opencv-python\n"
            "Or if that fails:\n"
            "  pip install --only-binary :all: opencv-python\n"
            "Or use an existing board image with --board-image option."
        )
    
    script_path = pathlib.Path(__file__).parent / "generate_charuco_board.py"
    cmd = [
        sys.executable,
        str(script_path),
        "--output", str(output_path),
    ]
    
    # パラメータを追加
    if "squares_x" in kwargs:
        cmd.extend(["--squares-x", str(kwargs["squares_x"])])
    if "squares_y" in kwargs:
        cmd.extend(["--squares-y", str(kwargs["squares_y"])])
    if "square_length_mm" in kwargs:
        cmd.extend(["--square-length-mm", str(kwargs["square_length_mm"])])
    if "marker_length_mm" in kwargs:
        cmd.extend(["--marker-length-mm", str(kwargs["marker_length_mm"])])
    if "dictionary" in kwargs:
        cmd.extend(["--dictionary", kwargs["dictionary"]])
    if "dpi" in kwargs:
        cmd.extend(["--dpi", str(kwargs["dpi"])])
    if "margin_mm" in kwargs:
        cmd.extend(["--margin-mm", str(kwargs["margin_mm"])])
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        error_msg = result.stderr if result.stderr else result.stdout
        raise RuntimeError(f"Failed to generate ChArUco board: {error_msg}")
    
    return output_path


def create_pdf_with_reportlab(
    output_pdf: pathlib.Path,
    board_image_path: pathlib.Path,
    squares_x: int,
    squares_y: int,
    square_length_mm: float,
    marker_length_mm: float,
) -> None:
    """reportlabを使用してPDFを生成"""
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    from reportlab.lib.fonts import addMapping
    
    # 日本語フォントの登録（macOSの場合）
    import glob
    japanese_font_name = None
    use_japanese = False
    
    try:
        # macOSの標準日本語フォントを検索
        font_search_paths = [
            "/System/Library/Fonts/**/*.ttc",
            "/System/Library/Fonts/**/*.ttf",
            "/Library/Fonts/**/*.ttc",
            "/Library/Fonts/**/*.ttf",
        ]
        
        # ヒラギノフォントを優先的に探す
        preferred_fonts = [
            "Hiragino Sans",
            "ヒラギノ",
            "Hiragino",
            "AppleGothic",
            "Osaka",
        ]
        
        found_fonts = []
        for pattern in font_search_paths:
            found_fonts.extend(glob.glob(pattern, recursive=True))
        
        # 優先フォントから順に試す
        for preferred in preferred_fonts:
            for font_path in found_fonts:
                if preferred.lower() in font_path.lower():
                    try:
                        pdfmetrics.registerFont(TTFont("JapaneseFont", font_path))
                        japanese_font_name = "JapaneseFont"
                        use_japanese = True
                        print(f"[INFO] Using Japanese font: {font_path}")
                        break
                    except Exception as e:
                        continue
            if use_japanese:
                break
        
        # 優先フォントが見つからない場合は、最初に見つかった日本語フォントを試す
        if not use_japanese:
            for font_path in found_fonts:
                # TTCファイルの場合、特定のフォントを指定する必要があるかもしれない
                try:
                    pdfmetrics.registerFont(TTFont("JapaneseFont", font_path))
                    japanese_font_name = "JapaneseFont"
                    use_japanese = True
                    print(f"[INFO] Using Japanese font: {font_path}")
                    break
                except Exception:
                    continue
        
        if not use_japanese:
            print("[WARN] Japanese font not found. Using English only.")
    except Exception as e:
        print(f"[WARN] Failed to register Japanese font: {e}. Using English only.")
        use_japanese = False
    
    board_width_mm = (squares_x - 1) * square_length_mm
    board_height_mm = (squares_y - 1) * square_length_mm
    
    # A3用紙サイズ（297mm × 420mm）でPDFを作成
    c = canvas.Canvas(str(output_pdf), pagesize=A3)
    width, height = A3
    
    # タイトル
    if use_japanese:
        c.setFont(japanese_font_name, 24)
        title = "キャリブレーション用 ChArUcoボード 印刷ガイド"
    else:
        c.setFont("Helvetica-Bold", 24)
        title = "ChArUco Board Printing Guide for Calibration"
    c.drawString(20 * mm, height - 30 * mm, title)
    
    # 日付
    if use_japanese:
        c.setFont(japanese_font_name, 10)
        date_str = datetime.now().strftime("%Y年%m月%d日")
        date_label = f"生成日: {date_str}"
    else:
        c.setFont("Helvetica", 10)
        date_str = datetime.now().strftime("%Y-%m-%d")
        date_label = f"Generated: {date_str}"
    c.drawString(20 * mm, height - 40 * mm, date_label)
    
    # ボード仕様
    y_pos = height - 60 * mm
    if use_japanese:
        c.setFont(japanese_font_name, 14)
        section_title = "ボード仕様"
    else:
        c.setFont("Helvetica-Bold", 14)
        section_title = "Board Specifications"
    c.drawString(20 * mm, y_pos, section_title)
    y_pos -= 15 * mm
    
    if use_japanese:
        c.setFont(japanese_font_name, 11)
        specs = [
            f"グリッドサイズ: {squares_x} × {squares_y}",
            f"チェス盤マス: {square_length_mm}mm × {square_length_mm}mm",
            f"ArUcoマーカー: {marker_length_mm}mm × {marker_length_mm}mm",
            f"ボードサイズ: {board_width_mm:.1f}mm × {board_height_mm:.1f}mm",
            f"ArUco辞書: DICT_4X4_50",
        ]
    else:
        c.setFont("Helvetica", 11)
        specs = [
            f"Grid size: {squares_x} × {squares_y}",
            f"Square size: {square_length_mm}mm × {square_length_mm}mm",
            f"ArUco marker: {marker_length_mm}mm × {marker_length_mm}mm",
            f"Board size: {board_width_mm:.1f}mm × {board_height_mm:.1f}mm",
            f"ArUco dictionary: DICT_4X4_50",
        ]
    for spec in specs:
        c.drawString(30 * mm, y_pos, spec)
        y_pos -= 12 * mm
    
    # ボード画像を配置
    if board_image_path.exists():
        try:
            # PILで画像を開いてサイズとDPIを確認
            from PIL import Image as PILImage
            pil_img = PILImage.open(str(board_image_path))
            img_dpi = pil_img.info.get('dpi', (300, 300))[0] if 'dpi' in pil_img.info else 300
            pil_img.close()
            
            # reportlabのImageReaderで画像を読み込み
            img = ImageReader(str(board_image_path))
            img_width_px, img_height_px = img.getSize()
            
            # 画像の物理サイズを計算（mm）
            # 画像は300 DPIで生成されているはず
            img_width_mm = (img_width_px / img_dpi) * 25.4
            img_height_mm = (img_height_px / img_dpi) * 25.4
            
            # ボード画像を実寸（+余白）で表示する場合のサイズ
            # 余白を含めた出力サイズ = ボードサイズ + 余白×2
            output_width_mm = board_width_mm + 20.0  # 余白10mm×2
            output_height_mm = board_height_mm + 20.0
            
            # A3用紙に収まるようにスケール（最大幅: 270mm、最大高さ: 380mm）
            # ただし、実寸に近いサイズで表示
            max_display_width = 270 * mm
            max_display_height = 380 * mm
            scale_x = max_display_width / (output_width_mm * mm / 25.4 * img_dpi) if img_width_px > 0 else 1.0
            scale_y = max_display_height / (output_height_mm * mm / 25.4 * img_dpi) if img_height_px > 0 else 1.0
            scale = min(scale_x, scale_y, 1.0)  # 拡大しない
            
            # 実際の表示サイズ（ポイント単位）
            display_width = img_width_px * scale
            display_height = img_height_px * scale
            
            # 中央に配置（上下の余白を考慮）
            x_img = (width - display_width) / 2
            y_img = height - 220 * mm - display_height  # 上部のテキスト領域を考慮
            
            # 画像を描画
            c.drawImage(img, x_img, y_img, width=display_width, height=display_height, preserveAspectRatio=True)
            
            # 画像の下に説明
            if use_japanese:
                c.setFont(japanese_font_name, 10)
                img_note = f"上記のChArUcoボードを実寸で印刷してください（ボードサイズ: {board_width_mm:.0f}mm × {board_height_mm:.0f}mm）"
            else:
                c.setFont("Helvetica", 10)
                img_note = f"Print the ChArUco board above at actual size (Board size: {board_width_mm:.0f}mm × {board_height_mm:.0f}mm)"
            c.drawString(x_img, y_img - 18 * mm, img_note)
            
        except Exception as e:
            print(f"[WARN] Failed to load board image: {e}")
            if use_japanese:
                c.setFont(japanese_font_name, 11)
                error_msg = f"ボード画像の読み込みに失敗しました: {board_image_path}"
            else:
                c.setFont("Helvetica", 11)
                error_msg = f"Failed to load board image: {board_image_path}"
            c.drawString(30 * mm, y_pos, error_msg)
            y_pos -= 15 * mm
    
    # 印刷手順
    y_pos = y_img - 50 * mm
    if use_japanese:
        c.setFont(japanese_font_name, 14)
        section_title = "印刷手順"
        steps = [
            "1. プリンタ設定を開き、以下の設定を確認してください:",
            "   • 用紙サイズ: A3以上（推奨）",
            "   • 印刷品質: 最高品質",
            "   • カラー: 白黒またはグレースケール",
            "",
            "2. スケール設定を「実際のサイズ」「100%」「余白なし印刷」に設定",
            "",
            f"3. 印刷後、定規で実寸を確認してください:",
            f"   • チェス盤マス: {square_length_mm}mm × {square_length_mm}mm",
            f"   • ArUcoマーカー: {marker_length_mm}mm × {marker_length_mm}mm",
            "",
            "4. 実寸が合わない場合は、スクリプトのパラメータを調整して再生成",
        ]
    else:
        c.setFont("Helvetica-Bold", 14)
        section_title = "Printing Instructions"
        steps = [
            "1. Open printer settings and verify the following:",
            "   • Paper size: A3 or larger (recommended)",
            "   • Print quality: Highest quality",
            "   • Color: Black and white or grayscale",
            "",
            "2. Set scale to 'Actual size', '100%', 'No margins'",
            "",
            f"3. After printing, verify actual size with a ruler:",
            f"   • Square size: {square_length_mm}mm × {square_length_mm}mm",
            f"   • ArUco marker: {marker_length_mm}mm × {marker_length_mm}mm",
            "",
            "4. If actual size does not match, adjust script parameters and regenerate",
        ]
    c.drawString(20 * mm, y_pos, section_title)
    y_pos -= 15 * mm
    
    for step in steps:
        if step.startswith("   "):
            font_size = 9
        else:
            font_size = 11
        if use_japanese:
            c.setFont(japanese_font_name, font_size)
        else:
            c.setFont("Helvetica", font_size)
        c.drawString(30 * mm, y_pos, step)
        y_pos -= 12 * mm
    
    # 注意事項
    y_pos -= 20 * mm
    if use_japanese:
        c.setFont(japanese_font_name, 14)
        section_title = "注意事項"
        notes = [
            "• ボードは平面に貼り付けて使用してください（歪みや折れがないこと）",
            "• ボードの4つの角のtoio Position ID座標を計測して設定ファイルに記録してください",
            "• 詳細な計測手順は docs/four_point_measurement.md を参照してください",
            "• ボードはtoioプレイマット上に固定し、キャリブレーション中は動かさないでください",
        ]
    else:
        c.setFont("Helvetica-Bold", 14)
        section_title = "Notes"
        notes = [
            "• Attach the board to a flat surface (no warping or creases)",
            "• Measure the toio Position ID coordinates of the 4 corners and record in config file",
            "• See docs/four_point_measurement.md for detailed measurement procedures",
            "• Fix the board on the toio playmat and do not move during calibration",
        ]
    c.drawString(20 * mm, y_pos, section_title)
    y_pos -= 15 * mm
    
    if use_japanese:
        c.setFont(japanese_font_name, 10)
    else:
        c.setFont("Helvetica", 10)
    for note in notes:
        c.drawString(30 * mm, y_pos, note)
        y_pos -= 12 * mm
    
    c.save()
    print(f"[INFO] PDF saved to {output_pdf}")


def create_pdf_with_pil(
    output_pdf: pathlib.Path,
    board_image_path: pathlib.Path,
) -> None:
    """PILを使用してシンプルなPDFを生成（画像のみ）"""
    if not board_image_path.exists():
        raise FileNotFoundError(f"Board image not found: {board_image_path}")
    
    img = Image.open(board_image_path)
    # RGBに変換（PDF用）
    if img.mode != "RGB":
        img = img.convert("RGB")
    
    img.save(str(output_pdf), "PDF", resolution=300.0)
    print(f"[INFO] PDF saved to {output_pdf} (image only)")
    print("[WARN] For full printing guide with instructions, install reportlab: pip install reportlab")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a PDF printing guide for ChArUco board."
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("charuco_board_printing_guide.pdf"),
        help="出力PDFファイルパス。",
    )
    parser.add_argument(
        "--board-image",
        type=pathlib.Path,
        default=None,
        help="既存のChArUcoボード画像（PNG）。指定しない場合は自動生成。",
    )
    parser.add_argument(
        "--squares-x",
        type=int,
        default=5,
        help="横方向のチェス盤マス数。既定 5。",
    )
    parser.add_argument(
        "--squares-y",
        type=int,
        default=7,
        help="縦方向のチェス盤マス数。既定 7。",
    )
    parser.add_argument(
        "--square-length-mm",
        type=float,
        default=45.0,
        help="チェス盤マス一辺の長さ [mm]。既定 45mm。",
    )
    parser.add_argument(
        "--marker-length-mm",
        type=float,
        default=33.0,
        help="ArUcoマーカー一辺の長さ [mm]。既定 33mm。",
    )
    parser.add_argument(
        "--dictionary",
        type=str,
        default="DICT_4X4_50",
        help="ArUco辞書名。既定 DICT_4X4_50。",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=300,
        help="ボード画像の解像度。既定 300。",
    )
    parser.add_argument(
        "--margin-mm",
        type=float,
        default=10.0,
        help="ボード周囲の余白 [mm]。既定 10mm。",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    
    # ボード画像を生成または使用
    if args.board_image and args.board_image.exists():
        board_image_path = args.board_image
        print(f"[INFO] Using existing board image: {board_image_path}")
    else:
        # 一時ファイルとしてボード画像を生成
        board_image_path = args.output.parent / f"charuco_board_temp_{args.output.stem}.png"
        print(f"[INFO] Generating ChArUco board image...")
        try:
            generate_charuco_board_image(
                board_image_path,
                squares_x=args.squares_x,
                squares_y=args.squares_y,
                square_length_mm=args.square_length_mm,
                marker_length_mm=args.marker_length_mm,
                dictionary=args.dictionary,
                dpi=args.dpi,
                margin_mm=args.margin_mm,
            )
        except Exception as e:
            print(f"[ERROR] Failed to generate board image: {e}", file=sys.stderr)
            return 1
    
    # PDFを生成
    try:
        if HAS_REPORTLAB:
            create_pdf_with_reportlab(
                args.output,
                board_image_path,
                args.squares_x,
                args.squares_y,
                args.square_length_mm,
                args.marker_length_mm,
            )
        elif HAS_PIL:
            create_pdf_with_pil(args.output, board_image_path)
        else:
            print("[ERROR] Neither reportlab nor PIL is available.", file=sys.stderr)
            print("[INFO] Install one of them:", file=sys.stderr)
            print("  pip install reportlab  # For full guide with instructions", file=sys.stderr)
            print("  pip install Pillow     # For simple PDF (image only)", file=sys.stderr)
            return 1
    except Exception as e:
        print(f"[ERROR] Failed to create PDF: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1
    
    # 一時ファイルを削除
    if not args.board_image and board_image_path.exists():
        board_image_path.unlink()
        print(f"[INFO] Temporary board image removed")
    
    print(f"\n[INFO] 印刷ガイドPDFが生成されました: {args.output}")
    print("[INFO] このPDFを開いて印刷してください。")
    
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)

