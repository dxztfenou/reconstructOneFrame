import argparse
import csv
import os
import re
import subprocess
import time
from pathlib import Path

os.environ.setdefault("OPENCV_IO_ENABLE_OPENEXR", "1")

import cv2
import numpy as np


def parse_frames(text: str, root: Path) -> list[int]:
    if not text:
        return sorted(int(child.name) for child in root.iterdir() if child.is_dir() and child.name.isdigit())

    frames: list[int] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if ":" in part:
            start_text, end_text = part.split(":", 1)
            start = int(start_text)
            end = int(end_text)
            step = 1 if end >= start else -1
            frames.extend(range(start, end + step, step))
        else:
            frames.append(int(part))
    return frames


def legacy_valid_count(root: Path, frame: int) -> int:
    exr_path = root / str(frame) / "0.exr"
    image = cv2.imread(str(exr_path), cv2.IMREAD_UNCHANGED)
    if image is None:
        raise RuntimeError(f"failed to read legacy EXR: {exr_path}")
    if image.ndim != 3 or image.shape[2] < 3:
        raise RuntimeError(f"legacy EXR is not CV_32FC3 XYZ: {exr_path}")

    xyz = image[:, :, :3]
    valid = np.isfinite(xyz).all(axis=2) & (np.linalg.norm(xyz, axis=2) > 0.0)
    return int(valid.sum())


def parse_sample_output(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        if line.startswith("qualitySummary="):
            values["qualitySummary"] = line.split("=", 1)[1].strip()
            continue
        if "=" in line and "," not in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()

    point_match = re.search(r"pointCloudVertexCount=(\d+)", output)
    if point_match:
        values["pointCloudVertexCount"] = point_match.group(1)
    run_match = re.search(r"runElapsedMs=([0-9.]+)", output)
    if run_match:
        values["runElapsedMs"] = run_match.group(1)
    return values


def run_frame(args: argparse.Namespace, frame: int) -> dict[str, object]:
    command = [
        str(args.exe),
        "--config",
        str(args.config),
        "--calib",
        str(args.calib),
        "--source-img-root",
        str(args.root),
        "--frame",
        str(frame),
    ]
    if args.write_ply:
        frame_output = args.output / "ply" / str(frame)
        command.extend(["--output", str(frame_output)])

    started = time.perf_counter()
    completed = subprocess.run(command, text=True, capture_output=True)
    process_ms = (time.perf_counter() - started) * 1000.0
    combined_output = completed.stdout + completed.stderr
    parsed = parse_sample_output(combined_output)

    legacy_count = legacy_valid_count(args.root, frame)
    our_count = int(parsed.get("pointCloudVertexCount", "0"))
    run_elapsed_ms = float(parsed.get("runElapsedMs", "nan"))
    ratio = our_count / legacy_count if legacy_count > 0 else float("nan")

    return {
        "frame": frame,
        "exitCode": completed.returncode,
        "status": parsed.get("status", ""),
        "legacyPointCount": legacy_count,
        "ourPointCount": our_count,
        "pointCountRatio": ratio,
        "runElapsedMs": run_elapsed_ms,
        "processElapsedMs": process_ms,
        "qualitySummary": parsed.get("qualitySummary", ""),
        "log": parsed.get("log", ""),
    }


def write_summary(rows: list[dict[str, object]], output: Path) -> None:
    comparable = [row for row in rows if int(row["legacyPointCount"]) > 0 and int(row["exitCode"]) == 0]
    losing = [row for row in comparable if float(row["pointCountRatio"]) < 0.95]
    legacy_zero_generated_nonzero = [
        row for row in rows
        if int(row["exitCode"]) == 0 and int(row["legacyPointCount"]) == 0 and int(row["ourPointCount"]) > 0
    ]
    run_times = [float(row["runElapsedMs"]) for row in comparable if np.isfinite(float(row["runElapsedMs"]))]
    legacy_counts = [int(row["legacyPointCount"]) for row in comparable]
    our_counts = [int(row["ourPointCount"]) for row in comparable]

    lines = [
        f"frames={len(rows)}",
        f"comparable_frames={len(comparable)}",
        f"point_count_losing_frames={len(losing)}",
        f"legacy_zero_generated_nonzero_frames={len(legacy_zero_generated_nonzero)}",
    ]
    if comparable:
        lines.extend(
            [
                f"legacy_points_total={sum(legacy_counts)}",
                f"our_points_total={sum(our_counts)}",
                f"total_point_count_ratio={sum(our_counts) / sum(legacy_counts):.6f}",
                f"min_point_count_ratio={min(float(row['pointCountRatio']) for row in comparable):.6f}",
            ]
        )
    if run_times:
        lines.extend(
            [
                f"run_elapsed_ms_median={float(np.median(run_times)):.3f}",
                f"run_elapsed_ms_p90={float(np.percentile(run_times, 90)):.3f}",
                f"run_elapsed_ms_mean={float(np.mean(run_times)):.3f}",
            ]
        )
    if losing:
        worst = sorted(losing, key=lambda row: float(row["pointCountRatio"]))[:20]
        lines.append("worst_point_count_frames=" + ",".join(f"{row['frame']}:{float(row['pointCountRatio']):.3f}" for row in worst))
    if legacy_zero_generated_nonzero:
        examples = legacy_zero_generated_nonzero[:20]
        lines.append("legacy_zero_generated_nonzero_examples=" + ",".join(f"{row['frame']}:{row['ourPointCount']}" for row in examples))

    (output / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark reconstructSample against output260707 gypsum legacy EXR point counts.")
    parser.add_argument("--root", type=Path, default=Path(r"D:\Data\output260707_gypsum\Upper"))
    parser.add_argument("--exe", type=Path, default=Path(r"build\Release\reconstructSample.exe"))
    parser.add_argument("--config", type=Path, default=Path(r"D:\Data\Calib\2607011016_mach6\singleStripe\output\run_config.json"))
    parser.add_argument("--calib", type=Path, default=Path(r"D:\Data\Calib\2607011016_mach6\calibParams.yml"))
    parser.add_argument("--frames", default="100,200,300,400", help="Comma list or inclusive ranges such as 0:465. Empty means all numeric frame dirs.")
    parser.add_argument("--output", type=Path, default=Path(r"build\big_battle_output260707_gypsum"))
    parser.add_argument("--write-ply", action="store_true", help="Also write depth_points.ply per frame. Disabled by default for timing.")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    rows = [run_frame(args, frame) for frame in parse_frames(args.frames, args.root)]

    csv_path = args.output / "benchmark.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = [
            "frame",
            "exitCode",
            "status",
            "legacyPointCount",
            "ourPointCount",
            "pointCountRatio",
            "runElapsedMs",
            "processElapsedMs",
            "qualitySummary",
            "log",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    write_summary(rows, args.output)
    print(f"csv={csv_path}")
    print(f"summary={args.output / 'summary.txt'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
