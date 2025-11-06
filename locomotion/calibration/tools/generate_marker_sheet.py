#!/usr/bin/env python3
"""
generate_marker_sheet.py
========================

OpenCV の ArUco マーカーを A4 用紙（既定）にレイアウトしたテンプレート画像を生成します。
デフォルトでは 45 mm 角マーカーを 2 列 × 4 行配置し、300 dpi の PNG を出力します。
印刷時はスケーリング 100%・余白なしを指定してください。
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from typing import Iterable, Tuple

import cv2
import numpy as np


MM_PER_INCH = 25.4


def mm_to_px(mm: float, dpi: int) -> int:
    return int(round(mm * dpi / MM_PER_INCH))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate an ArUco marker sheet for printing.")
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("markers_a4_45mm.png"),
        help="出力ファイルパス（PNG）。",
    )
    parser.add_argument(
        "--dpi", type=int, default=300, help="ページ解像度 (dots per inch)。既定 300。"
    )
    parser.add_argument(
        "--marker-size-mm",
        type=float,
        default=45.0,
        help="マーカー一辺の長さ [mm]。既定 45mm。",
    )
    parser.add_argument(
        "--border-mm",
        type=float,
        default=3.0,
        help="マーカー周囲の白マージン [mm]。既定 3mm。",
    )
    parser.add_argument(
        "--page-width-mm",
        type=float,
        default=210.0,
        help="用紙幅 [mm]。既定 210mm (A4 縦)。",
    )
    parser.add_argument(
        "--page-height-mm",
        type=float,
        default=297.0,
        help="用紙高さ [mm]。既定 297mm (A4 縦)。",
    )
    parser.add_argument(
        "--columns", type=int, default=2, help="横方向のマーカー枚数。既定 2。"
    )
    parser.add_argument(
        "--rows", type=int, default=4, help="縦方向のマーカー枚数。既定 4。"
    )
    parser.add_argument(
        "--start-id",
        type=int,
        default=0,
        help="最初のマーカー ID。以降は +1 ずつ増加。",
    )
    parser.add_argument(
        "--dictionary",
        type=str,
        default="DICT_4X4_50",
        help="ArUco 辞書名（cv::aruco::PREDEFINED_DICTIONARY_NAME）。",
    )
    parser.add_argument(
        "--individual-dir",
        type=pathlib.Path,
        default=None,
        help="各マーカー単体の PNG を保存するディレクトリ（省略時は保存しない）。",
    )
    parser.add_argument(
        "--metadata",
        type=pathlib.Path,
        default=None,
        help="生成したマーカーの ID と配置情報を JSON で保存するパス。",
    )
    return parser.parse_args()


def get_aruco_dictionary(name: str) -> cv2.aruco.Dictionary:
    if not name.startswith("DICT_"):
        raise ValueError(f"辞書名 '{name}' が認識できません。'DICT_4X4_50' のように指定してください。")
    attr = getattr(cv2.aruco, name, None)
    if attr is None:
        raise ValueError(f"OpenCV で '{name}' 辞書がサポートされていません。")
    return cv2.aruco.getPredefinedDictionary(attr)


def draw_marker_image(dictionary: cv2.aruco.Dictionary, marker_id: int, size_px: int) -> np.ndarray:
    """Generate an ArUco marker image, supporting both legacy drawMarker and new generateImageMarker APIs."""
    if hasattr(cv2.aruco, "generateImageMarker"):  # OpenCV ≥ 4.10
        return cv2.aruco.generateImageMarker(dictionary, marker_id, size_px)
    img = np.zeros((size_px, size_px), dtype=np.uint8)
    cv2.aruco.drawMarker(dictionary, marker_id, size_px, img, borderBits=1)
    return img


def add_label(canvas: np.ndarray, text: str, origin: Tuple[int, int], font_scale: float = 0.5) -> None:
    font = cv2.FONT_HERSHEY_SIMPLEX
    thickness = 1
    color = (0, 0, 0)
    cv2.putText(canvas, text, origin, font, font_scale, color, thickness, cv2.LINE_AA)


def main() -> int:
    args = parse_args()

    dictionary = get_aruco_dictionary(args.dictionary)
    total_markers = args.columns * args.rows

    marker_px = mm_to_px(args.marker_size_mm, args.dpi)
    border_px = mm_to_px(args.border_mm, args.dpi)
    page_w_px = mm_to_px(args.page_width_mm, args.dpi)
    page_h_px = mm_to_px(args.page_height_mm, args.dpi)

    if page_w_px <= 0 or page_h_px <= 0:
        raise ValueError("ページサイズが無効です。")
    if marker_px <= 0:
        raise ValueError("マーカーサイズが小さすぎます。")

    canvas = np.full((page_h_px, page_w_px, 3), 255, dtype=np.uint8)
    cell_w = marker_px + 2 * border_px
    cell_h = marker_px + 2 * border_px

    grid_w = args.columns * cell_w
    grid_h = args.rows * cell_h

    if grid_w > page_w_px or grid_h > page_h_px:
        raise ValueError("レイアウトがページサイズに収まりません。列/行数かサイズを調整してください。")

    offset_x = (page_w_px - grid_w) // 2
    offset_y = (page_h_px - grid_h) // 2

    per_marker = []

    individual_dir = args.individual_dir
    if individual_dir is not None:
        individual_dir.mkdir(parents=True, exist_ok=True)

    for idx in range(total_markers):
        marker_id = args.start_id + idx
        row = idx // args.columns
        col = idx % args.columns

        x = offset_x + col * cell_w + border_px
        y = offset_y + row * cell_h + border_px

        marker_img = draw_marker_image(dictionary, marker_id, marker_px)

        canvas[y : y + marker_px, x : x + marker_px, 0] = marker_img
        canvas[y : y + marker_px, x : x + marker_px, 1] = marker_img
        canvas[y : y + marker_px, x : x + marker_px, 2] = marker_img

        label_y = min(page_h_px - 10, y + marker_px + border_px // 2 + 12)
        label_x = x
        add_label(canvas, f"ID {marker_id}", (label_x, label_y))

        per_marker.append(
            {
                "id": marker_id,
                "row": row,
                "column": col,
                "top_left_px": [int(x), int(y)],
                "marker_size_px": marker_px,
            }
        )

        if individual_dir is not None:
            path = individual_dir / f"marker_{marker_id:03d}.png"
            cv2.imwrite(str(path), marker_img)

    cv2.imwrite(str(args.output), canvas)
    print(f"[INFO] Marker sheet saved to {args.output}")

    if args.metadata is not None:
        metadata = {
            "output": str(args.output),
            "dpi": args.dpi,
            "page_size_mm": [args.page_width_mm, args.page_height_mm],
            "marker_size_mm": args.marker_size_mm,
            "border_mm": args.border_mm,
            "dictionary": args.dictionary,
            "start_id": args.start_id,
            "rows": args.rows,
            "columns": args.columns,
            "markers": per_marker,
        }
        args.metadata.parent.mkdir(parents=True, exist_ok=True)
        args.metadata.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        print(f"[INFO] Metadata saved to {args.metadata}")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"[ERROR] {exc}", file=sys.stderr)
        sys.exit(1)
