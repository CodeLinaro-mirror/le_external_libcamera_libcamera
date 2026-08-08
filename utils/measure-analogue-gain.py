#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026, John Cronin <john.cronin@opcenter.com>
#
# Measure camera sensor analogue gain response and estimate black level
# for CameraSensorHelper development.
#
# Dependencies: libcamera "cam" tool, v4l2-ctl (v4l-utils), Python 3.10+.
#
# Example:
#   ./utils/measure-analogue-gain.py \
#       --sensor-name imx471 \
#       --width 1928 --height 1088 --stride 3904 --bit-depth 10 \
#       --exposure 200 --digital-gain 256 \
#       --gains 0,50,100,150,200,300,400,500,600,700,800 \
#       --out /tmp/gain-measure

from __future__ import annotations

import argparse
import csv
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path


def resolve_cam_cmd() -> list[str]:
    """Return argv prefix to invoke the libcamera cam utility.

    LIBCAMERA_CAM may be set to a command string (for example a path to
    cam, or a wrapper that sets LD_LIBRARY_PATH for a local install).
    """
    env = os.environ.get("LIBCAMERA_CAM")
    if env:
        return env.split()
    return ["cam"]


def find_subdev(name_prefix: str) -> str:
    for path in sorted(Path("/dev").glob("v4l-subdev*")):
        name_file = Path("/sys/class/video4linux") / path.name / "name"
        try:
            name = name_file.read_text(encoding="utf-8").strip()
        except OSError:
            continue
        if name.startswith(name_prefix):
            return str(path)
    raise SystemExit(f"No v4l-subdev with name prefix {name_prefix!r} found")


def v4l2_set(subdev: str, **ctrls: int) -> None:
    arg = ",".join(f"{name}={value}" for name, value in ctrls.items())
    subprocess.run(
        ["v4l2-ctl", "-d", subdev, f"--set-ctrl={arg}"],
        check=False,
        capture_output=True,
    )


def v4l2_get(subdev: str, *names: str) -> dict[str, int]:
    result = subprocess.run(
        ["v4l2-ctl", "-d", subdev, f"--get-ctrl={','.join(names)}"],
        check=False,
        capture_output=True,
        text=True,
    )
    out: dict[str, int] = {}
    for line in result.stdout.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        out[key.strip()] = int(value.strip())
    return out


def frame_stats(
    path: Path,
    width: int,
    height: int,
    stride: int,
    bit_depth: int,
) -> dict[str, float]:
    """Compute basic stats for a packed raw frame.

    Expects little-endian 16-bit samples per pixel with the useful bits
    in the low ``bit_depth`` bits (common for 10-bit Bayer delivered as
    16-bit words). ``stride`` is the bytes-per-line reported by the
    capture pipeline (may include padding).
    """
    frame_size = stride * height
    data = path.read_bytes()
    if len(data) < frame_size:
        raise ValueError(f"{path}: got {len(data)} bytes, need {frame_size}")

    mask = (1 << bit_depth) - 1
    vals: list[int] = []
    # Subsample active area for speed; skip a small border.
    for y in range(8, height - 8, 4):
        row = data[y * stride : y * stride + width * 2]
        for x in range(8, width - 8, 4):
            sample = row[x * 2] | (row[x * 2 + 1] << 8)
            vals.append(sample & mask)

    vals.sort()
    count = len(vals)
    return {
        "n": count,
        "min": vals[0],
        "p1": vals[max(0, count // 100)],
        "p5": vals[max(0, count // 20)],
        "p50": vals[count // 2],
        "p95": vals[min(count - 1, (count * 95) // 100)],
        "max": vals[-1],
        "mean": statistics.fmean(vals),
    }


def capture_raw(
    out_dir: Path,
    tag: str,
    camera: str,
    width: int,
    height: int,
    frame_size: int,
    count: int,
) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    pattern = str(out_dir / f"{tag}-#.bin")
    cmd = resolve_cam_cmd() + [
        "--camera",
        camera,
        "--stream",
        f"role=raw,width={width},height={height}",
        f"--capture={count}",
        f"--file={pattern}",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
    if result.returncode != 0:
        sys.stderr.write(result.stderr or result.stdout or "cam failed\n")

    files = sorted(out_dir.glob(f"{tag}-*.bin"))
    if not files:
        files = sorted(out_dir.glob(f"{tag}*"))
    return [path for path in files if path.stat().st_size >= frame_size]


def measure_at_gain(
    subdev: str,
    out_dir: Path,
    gain: int,
    exposure: int,
    digital_gain: int | None,
    camera: str,
    width: int,
    height: int,
    stride: int,
    bit_depth: int,
    settle: int = 1,
) -> dict:
    ctrls: dict[str, int] = {"exposure": exposure, "analogue_gain": gain}
    if digital_gain is not None:
        ctrls["digital_gain"] = digital_gain
    v4l2_set(subdev, **ctrls)

    frame_size = stride * height
    files = capture_raw(
        out_dir,
        f"g{gain:04d}",
        camera=camera,
        width=width,
        height=height,
        frame_size=frame_size,
        count=settle + 2,
    )
    readback_names = ["exposure", "analogue_gain"]
    if digital_gain is not None:
        readback_names.append("digital_gain")
    readback = v4l2_get(subdev, *readback_names)

    use = files[settle:] if len(files) > settle else files
    if not use:
        raise RuntimeError(f"no frames captured for analogue_gain={gain}")

    stats_list = [
        frame_stats(path, width, height, stride, bit_depth) for path in use
    ]
    means = [item["mean"] for item in stats_list]
    return {
        "request_gain": gain,
        "request_exposure": exposure,
        "request_digital_gain": digital_gain,
        "readback": readback,
        "mean": statistics.fmean(means),
        "mean_std": statistics.pstdev(means) if len(means) > 1 else 0.0,
        "p1": statistics.fmean([item["p1"] for item in stats_list]),
        "p50": statistics.fmean([item["p50"] for item in stats_list]),
        "p95": statistics.fmean([item["p95"] for item in stats_list]),
        "max": max(item["max"] for item in stats_list),
        "frames": [str(path) for path in use],
        "per_frame": stats_list,
    }


def predicted_linear(code: int, k: int) -> float:
    if code >= k:
        return float("inf")
    return k / (k - code)


def fit_linear_k(results: list[dict], black: float, k_min: int, k_max: int) -> dict:
    sig0 = max(1e-6, results[0]["mean"] - black)
    best: tuple[float, int] | None = None
    for k in range(k_min, k_max + 1):
        error = 0.0
        count = 0
        for row in results:
            code = row["request_gain"]
            if code >= k:
                continue
            ratio = max(0.0, row["mean"] - black) / sig0
            pred = predicted_linear(code, k)
            error += (ratio - pred) ** 2
            count += 1
        if not count:
            continue
        mse = error / count
        if best is None or mse < best[0]:
            best = (mse, k)
    if best is None:
        return {}
    return {"mse": best[0], "k": best[1]}


def parse_gains(text: str) -> list[int]:
    return [int(part) for part in text.split(",") if part.strip() != ""]


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Measure analogue gain response and estimate black level for "
            "CameraSensorHelper development."
        )
    )
    parser.add_argument(
        "--sensor-name",
        default="imx471",
        help="v4l-subdev sysfs name prefix (default: imx471)",
    )
    parser.add_argument("--subdev", help="override sensor subdev path")
    parser.add_argument(
        "--camera",
        default="1",
        help="libcamera camera id or index for cam (default: 1)",
    )
    parser.add_argument("--width", type=int, default=1928)
    parser.add_argument("--height", type=int, default=1088)
    parser.add_argument(
        "--stride",
        type=int,
        default=3904,
        help="bytes per line of raw frames (may include padding)",
    )
    parser.add_argument(
        "--bit-depth",
        type=int,
        default=10,
        help="useful bits per sample in the 16-bit containers (default: 10)",
    )
    parser.add_argument("--exposure", type=int, default=200)
    parser.add_argument(
        "--digital-gain",
        type=int,
        default=256,
        help="digital gain code, or -1 to leave untouched (default: 256)",
    )
    parser.add_argument(
        "--gains",
        default="0,50,100,150,200,300,400,500,600,700,800",
        help="comma-separated analogue_gain codes to measure",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("/tmp/libcamera-gain-measure"),
        help="output directory for frames and reports",
    )
    parser.add_argument(
        "--dark",
        action="store_true",
        help="measure near-black at gain 0 with short exposures",
    )
    parser.add_argument(
        "--model-k",
        type=int,
        default=1024,
        help="reference linear model constant for G=k/(k-code) (default: 1024)",
    )
    args = parser.parse_args()

    digital_gain = None if args.digital_gain < 0 else args.digital_gain
    subdev = args.subdev or find_subdev(args.sensor_name)
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    print(f"cam cmd: {' '.join(resolve_cam_cmd())}", flush=True)
    print(
        f"subdev={subdev} camera={args.camera} "
        f"{args.width}x{args.height} stride={args.stride} "
        f"bit_depth={args.bit_depth}",
        flush=True,
    )

    if args.dark:
        rows = []
        for exposure in (1, 10, 50):
            row = measure_at_gain(
                subdev,
                out / "dark",
                gain=0,
                exposure=exposure,
                digital_gain=digital_gain,
                camera=args.camera,
                width=args.width,
                height=args.height,
                stride=args.stride,
                bit_depth=args.bit_depth,
            )
            rows.append(row)
            print(
                f"dark exp={exposure} mean={row['mean']:.2f} "
                f"p1={row['p1']:.1f} p50={row['p50']:.1f} "
                f"readback={row['readback']}",
                flush=True,
            )
        (out / "dark.json").write_text(json.dumps(rows, indent=2) + "\n")
        print(f"Wrote {out / 'dark.json'}", flush=True)
        return 0

    gains = parse_gains(args.gains)
    results: list[dict] = []
    print(
        f"exposure={args.exposure} digital_gain={digital_gain}",
        flush=True,
    )
    print(
        "gain\treadback\tmean\tp1\tp50\tp95\t"
        f"pred_k{args.model_k}\tratio_vs_g0",
        flush=True,
    )

    base_mean = None
    base_p1 = None
    for gain in gains:
        row = measure_at_gain(
            subdev,
            out / "sweep",
            gain=gain,
            exposure=args.exposure,
            digital_gain=digital_gain,
            camera=args.camera,
            width=args.width,
            height=args.height,
            stride=args.stride,
            bit_depth=args.bit_depth,
        )
        results.append(row)
        if base_mean is None:
            base_mean = row["mean"]
            base_p1 = row["p1"]

        black = base_p1 if base_p1 is not None else 0.0
        signal = max(0.0, row["mean"] - black)
        signal0 = max(1e-6, (base_mean or row["mean"]) - black)
        ratio = signal / signal0
        pred = predicted_linear(gain, args.model_k)
        readback_gain = row["readback"].get("analogue_gain", -1)
        print(
            f"{gain}\t{readback_gain}\t{row['mean']:.2f}\t{row['p1']:.1f}\t"
            f"{row['p50']:.1f}\t{row['p95']:.1f}\t{pred:.4f}\t{ratio:.4f}",
            flush=True,
        )

    black = results[0]["p1"]
    signal0 = max(1e-6, results[0]["mean"] - black)
    points = []
    for row in results:
        code = row["request_gain"]
        signal = max(0.0, row["mean"] - black)
        ratio = signal / signal0
        pred = predicted_linear(code, args.model_k)
        points.append(
            {
                "code": code,
                "mean": row["mean"],
                "signal": signal,
                "ratio_measured": ratio,
                f"ratio_pred_k{args.model_k}": pred,
                "rel_error": (ratio - pred) / pred if pred else None,
                "readback_gain": row["readback"].get("analogue_gain"),
            }
        )

    best = fit_linear_k(results, black, k_min=max(args.model_k // 2, 2), k_max=args.model_k * 2)
    analysis = {
        "black_estimate_p1_at_g0": black,
        "black_level_16bit": int(round(black)) << (16 - args.bit_depth),
        "exposure": args.exposure,
        "digital_gain": digital_gain,
        "reference_model": f"G = {args.model_k}/({args.model_k}-code)",
        "best_linear_k_mse": best,
        "points": points,
    }
    summary = {"results": results, "analysis": analysis}
    (out / "results.json").write_text(json.dumps(summary, indent=2) + "\n")

    with (out / "results.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "code",
                "readback",
                "mean",
                "p1",
                "p50",
                "p95",
                "signal",
                "ratio",
                f"pred_k{args.model_k}",
            ]
        )
        for point in points:
            row = next(item for item in results if item["request_gain"] == point["code"])
            writer.writerow(
                [
                    point["code"],
                    point["readback_gain"],
                    f"{row['mean']:.4f}",
                    f"{row['p1']:.2f}",
                    f"{row['p50']:.2f}",
                    f"{row['p95']:.2f}",
                    f"{point['signal']:.4f}",
                    f"{point['ratio_measured']:.6f}",
                    f"{point[f'ratio_pred_k{args.model_k}']:.6f}",
                ]
            )

    print(f"\nWrote {out / 'results.json'} and {out / 'results.csv'}", flush=True)
    print(
        f"Black estimate p1@g0={black:.2f} DN "
        f"-> blackLevel_={analysis['black_level_16bit']} at 16-bit",
        flush=True,
    )
    if best:
        print(
            f"Best linear k for G=k/(k-code): k={best['k']} mse={best['mse']:.6f}",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
