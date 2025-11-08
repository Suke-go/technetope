#!/usr/bin/env python3
"""
Generate a SoundTimeline JSON that plays a preset (default: sample.wav)
across all known M5Stick devices with a fixed delay between each trigger.

Example:
    python3 acoustics/tools/timeline/generate_sample_staggered.py \\
        --spacing 23 \\
        --passes 1 \\
        --output acoustics/pc_tools/scheduler/examples/sample_staggered.json
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import List


SCRIPT_PATH = Path(__file__).resolve()
REPO_ROOT = SCRIPT_PATH.parents[3]
DEFAULT_DEVICES = REPO_ROOT / "state" / "devices.json"
DEFAULT_OUTPUT = (
    REPO_ROOT
    / "acoustics"
    / "pc_tools"
    / "scheduler"
    / "examples"
    / "sample_staggered.json"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate a timeline that staggers preset playback across all"
            " devices listed in state/devices.json."
        )
    )
    parser.add_argument(
        "--devices",
        type=Path,
        default=DEFAULT_DEVICES,
        help=f"Device registry (default: {DEFAULT_DEVICES})",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Output timeline path (default: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--preset",
        default="sample",
        help="Preset ID to trigger (default: sample).",
    )
    parser.add_argument(
        "--gain",
        type=float,
        default=1.0,
        help="Gain multiplier passed to /acoustics/play (default: 1.0).",
    )
    parser.add_argument(
        "--spacing",
        type=float,
        default=23.0,
        help="Seconds to wait before triggering the next device (default: 23).",
    )
    parser.add_argument(
        "--passes",
        type=int,
        default=1,
        help="How many times to iterate over the device list (default: 1).",
    )
    parser.add_argument(
        "--start-offset",
        type=float,
        default=0.0,
        help="Seconds before the first trigger (default: 0).",
    )
    parser.add_argument(
        "--lead-time",
        type=float,
        default=8.0,
        help="Timeline default_lead_time value (default: 8.0).",
    )
    return parser.parse_args()


def load_device_ids(devices_path: Path) -> List[str]:
    payload = json.loads(devices_path.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise ValueError(f"{devices_path} must contain a JSON list")

    device_ids: List[str] = []
    for entry in payload:
        if not isinstance(entry, dict):
            continue
        device_id = entry.get("id") or entry.get("device_id") or entry.get("alias")
        if not device_id:
            continue
        if device_id not in device_ids:
            device_ids.append(device_id)

    if not device_ids:
        raise ValueError(f"No device IDs found in {devices_path}")
    return device_ids


def build_events(
    device_ids: List[str],
    preset: str,
    gain: float,
    spacing: float,
    passes: int,
    start_offset: float,
) -> List[dict]:
    if spacing <= 0:
        raise ValueError("spacing must be > 0")
    if passes < 1:
        raise ValueError("passes must be >= 1")

    events: List[dict] = []
    sequence_length = len(device_ids)

    for pass_index in range(passes):
        for index, device_id in enumerate(device_ids):
            position = (pass_index * sequence_length) + index
            offset = round(start_offset + position * spacing, 6)
            events.append(
                {
                    "offset": offset,
                    "address": "/acoustics/play",
                    "targets": [device_id],
                    "args": [preset, 0, gain, 0],
                }
            )
    return events


def build_timeline(
    device_ids: List[str],
    preset: str,
    gain: float,
    spacing: float,
    passes: int,
    start_offset: float,
    lead_time: float,
) -> dict:
    events = build_events(
        device_ids=device_ids,
        preset=preset,
        gain=gain,
        spacing=spacing,
        passes=passes,
        start_offset=start_offset,
    )

    description = (
        "Sequential playback with {spacing}s spacing across {count} device(s), "
        "{passes} pass(es)."
    ).format(
        spacing=spacing,
        count=len(device_ids),
        passes=passes,
    )

    return {
        "version": "1.2",
        "default_lead_time": lead_time,
        "metadata": {
            "title": f"{preset} staggered playback",
            "description": description,
            "device_count": len(device_ids),
            "passes": passes,
            "spacing_seconds": spacing,
        },
        "events": events,
    }


def main() -> None:
    args = parse_args()
    device_ids = load_device_ids(args.devices)
    timeline = build_timeline(
        device_ids=device_ids,
        preset=args.preset,
        gain=args.gain,
        spacing=args.spacing,
        passes=args.passes,
        start_offset=args.start_offset,
        lead_time=args.lead_time,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(timeline, indent=2) + "\n", encoding="utf-8")
    print(
        f"Wrote timeline for {len(device_ids)} device(s) "
        f"({args.passes} pass(es)) to {args.output}"
    )


if __name__ == "__main__":
    main()
