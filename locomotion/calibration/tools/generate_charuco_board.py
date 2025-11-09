#!/usr/bin/env python3
"""
generate_charuco_board.py
==========================

ChArUcoボード（チェス盤 + ArUcoマーカー）を生成するスクリプトです。
キャリブレーション用の5x7グリッドボードを生成します。

デフォルト設定:
- 5x7グリッド (squares_x=5, squares_y=7)
- 正方形サイズ: 45mm
- マーカーサイズ: 33mm
- ArUco辞書: DICT_4X4_50
- 出力解像度: 300 dpi
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

import cv2
import numpy as np


MM_PER_INCH = 25.4


def mm_to_px(mm: float, dpi: int) -> int:
    """mmをピクセルに変換"""
    return int(round(mm * dpi / MM_PER_INCH))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a ChArUco board for calibration."
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("charuco_board_5x7_45mm.png"),
        help="出力ファイルパス（PNG）。",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=300,
        help="出力解像度 (dots per inch)。既定 300。",
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
        help="ArUco辞書名（cv::aruco::PREDEFINED_DICTIONARY_NAME）。既定 DICT_4X4_50。",
    )
    parser.add_argument(
        "--margin-mm",
        type=float,
        default=10.0,
        help="ボード周囲の余白 [mm]。既定 10mm。",
    )
    parser.add_argument(
        "--metadata",
        type=pathlib.Path,
        default=None,
        help="生成したボードの情報をJSONで保存するパス。",
    )
    return parser.parse_args()


def get_aruco_dictionary(name: str) -> cv2.aruco.Dictionary:
    """ArUco辞書を取得"""
    if not name.startswith("DICT_"):
        raise ValueError(
            f"辞書名 '{name}' が認識できません。'DICT_4X4_50' のように指定してください。"
        )
    attr = getattr(cv2.aruco, name, None)
    if attr is None:
        raise ValueError(f"OpenCV で '{name}' 辞書がサポートされていません。")
    return cv2.aruco.getPredefinedDictionary(attr)


def main() -> int:
    args = parse_args()

    # ArUco辞書を取得
    dictionary = get_aruco_dictionary(args.dictionary)

    # ChArUcoボードを作成
    # 注意: OpenCVのCharucoBoardは (width, height) = (squares_x, squares_y) の順序
    board_size = (args.squares_x, args.squares_y)
    
    # mmをメートルに変換
    square_length_m = args.square_length_mm / 1000.0
    marker_length_m = args.marker_length_mm / 1000.0

    board = cv2.aruco.CharucoBoard(
        board_size, square_length_m, marker_length_m, dictionary
    )
    board.setLegacyPattern(True)

    # ボードの実サイズを計算（mm）
    board_width_mm = (args.squares_x - 1) * args.square_length_mm
    board_height_mm = (args.squares_y - 1) * args.square_length_mm

    # 余白を含めた出力サイズを計算
    output_width_mm = board_width_mm + 2 * args.margin_mm
    output_height_mm = board_height_mm + 2 * args.margin_mm

    # ピクセルサイズに変換
    output_width_px = mm_to_px(output_width_mm, args.dpi)
    output_height_px = mm_to_px(output_height_mm, args.dpi)
    margin_px = mm_to_px(args.margin_mm, args.dpi)

    print(f"[INFO] Board size: {board_width_mm:.1f}mm × {board_height_mm:.1f}mm")
    print(f"[INFO] Output size: {output_width_mm:.1f}mm × {output_height_mm:.1f}mm")
    print(f"[INFO] Output resolution: {output_width_px}px × {output_height_px}px @ {args.dpi} dpi")

    # ボード画像を生成
    board_image = board.generateImage(
        (output_width_px, output_height_px), marginSize=margin_px
    )

    # PNGとして保存（DPI情報を含める）
    # OpenCVのimwriteはDPI情報を保存しないため、PILを使用
    try:
        from PIL import Image
        import numpy as np
        
        # OpenCV画像をPIL画像に変換
        if len(board_image.shape) == 2:
            # グレースケール
            pil_image = Image.fromarray(board_image, mode='L')
        else:
            # カラー
            pil_image = Image.fromarray(cv2.cvtColor(board_image, cv2.COLOR_BGR2RGB))
        
        # DPI情報を設定して保存
        pil_image.save(str(args.output), 'PNG', dpi=(args.dpi, args.dpi))
        print(f"[INFO] ChArUco board saved to {args.output} (DPI: {args.dpi})")
    except ImportError:
        # PILが使えない場合はOpenCVで保存
        cv2.imwrite(str(args.output), board_image)
        print(f"[INFO] ChArUco board saved to {args.output} (PIL not available, DPI info not saved)")

    # メタデータを保存
    if args.metadata is not None:
        metadata = {
            "output": str(args.output),
            "dpi": args.dpi,
            "squares_x": args.squares_x,
            "squares_y": args.squares_y,
            "square_length_mm": args.square_length_mm,
            "marker_length_mm": args.marker_length_mm,
            "dictionary": args.dictionary,
            "margin_mm": args.margin_mm,
            "board_width_mm": board_width_mm,
            "board_height_mm": board_height_mm,
            "output_width_mm": output_width_mm,
            "output_height_mm": output_height_mm,
            "output_width_px": output_width_px,
            "output_height_px": output_height_px,
            "notes": [
                "印刷時はスケール100%で印刷してください。",
                f"チェス盤マスは実寸 {args.square_length_mm}mm であることを確認してください。",
                f"ArUcoマーカーは実寸 {args.marker_length_mm}mm であることを確認してください。",
                "印刷後、定規で実寸を確認し、必要に応じて --square-length-mm や --marker-length-mm を調整して再生成してください。",
            ],
        }
        args.metadata.parent.mkdir(parents=True, exist_ok=True)
        args.metadata.write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"[INFO] Metadata saved to {args.metadata}")

    print("\n[INFO] 印刷時の注意:")
    print("  1. プリンタ設定で「実際のサイズ」「100%」「余白なし印刷」を選択")
    print(f"  2. チェス盤マスが実寸 {args.square_length_mm}mm か定規で確認")
    print(f"  3. ArUcoマーカーが実寸 {args.marker_length_mm}mm か定規で確認")
    print("  4. 必要に応じてパラメータを調整して再生成")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)

