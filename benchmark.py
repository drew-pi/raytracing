#!/usr/bin/env python3

import argparse
import csv
import math
import re
import statistics
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build-profile"

CHECKSUM_PATTERN = re.compile(r"checksum:\s*([^\s]+)\s+([^\s]+)\s+([^\s]+)")
TIMING_PATTERN = re.compile(r"scene_ms,build_ms,render_ms\s*\n([^\r\n]+)")


def build():
    configure = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(BUILD),
        "-DCMAKE_CXX_COMPILER=g++",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O3 -g -DNDEBUG -march=native",
    ]

    # Reapply settings if changing compilers resets an existing CMake cache.
    subprocess.run(configure, check=True)
    subprocess.run(configure, check=True)

    subprocess.run(
        [
            "cmake",
            "--build",
            str(BUILD),
            "--config",
            "RelWithDebInfo",
            "--target",
            "raytracer_bench",
            "--parallel",
        ],
        check=True,
    )


def find_executable():
    # Support single-configuration and multi-configuration generators.
    candidates = [
        BUILD / "RelWithDebInfo" / "raytracer_bench",
        BUILD / "raytracer_bench",
    ]

    for candidate in candidates:
        if candidate.is_file():
            return candidate

    raise RuntimeError("Cannot find raytracer_bench; build it first.")


def run_trial(executable, log_path):
    with log_path.open("wb") as log:
        result = subprocess.run(
            [str(executable)],
            cwd=ROOT,
            stdout=subprocess.DEVNULL,
            stderr=log,
        )

    if result.returncode != 0:
        raise RuntimeError(
            f"Benchmark exited with code {result.returncode}; see {log_path}"
        )

    text = log_path.read_text(errors="replace")
    checksums = list(CHECKSUM_PATTERN.finditer(text))
    timings = list(TIMING_PATTERN.finditer(text))

    if len(checksums) != 1 or len(timings) != 1:
        raise RuntimeError(
            f"Expected exactly one checksum and timing row; see {log_path}"
        )

    checksum = checksums[0].groups()

    try:
        checksum_values = [float(value) for value in checksum]
        times = [float(value.strip()) for value in timings[0].group(1).split(",")]
    except ValueError as error:
        raise RuntimeError(f"Invalid numeric output; see {log_path}") from error

    if not all(math.isfinite(value) for value in checksum_values):
        raise RuntimeError(f"Nonfinite checksum; see {log_path}")

    if len(times) != 3 or not all(
        math.isfinite(value) and value >= 0 for value in times
    ):
        raise RuntimeError(f"Invalid phase timings; see {log_path}")

    return checksum, [*times, sum(times)]


def summarize(rows):
    print("\nTimes in milliseconds:")
    print(
        f"{'Phase':<10} {'Mean':>12} {'Median':>12} "
        f"{'Min':>12} {'Max':>12} {'Stddev':>12}"
    )

    for column, name in enumerate(("Scene", "Build", "Render", "Total")):
        values = [row[column] for row in rows]
        deviation = statistics.stdev(values) if len(values) > 1 else 0.0

        print(
            f"{name:<10} "
            f"{statistics.mean(values):12.3f} "
            f"{statistics.median(values):12.3f} "
            f"{min(values):12.3f} "
            f"{max(values):12.3f} "
            f"{deviation:12.3f}"
        )


def main():
    parser = argparse.ArgumentParser(
        description="Build and repeatedly benchmark the ray tracer."
    )
    parser.add_argument(
        "-n",
        "--runs",
        type=int,
        default=5,
        help="number of measured runs (default: 5)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="use the existing build-profile executable",
    )
    parser.add_argument(
        "--name",
        help="run label used in the results directory name (e.g. cached-stack)",
    )
    args = parser.parse_args()

    if args.runs < 1:
        parser.error("--runs must be at least 1")

    if args.name is not None and not re.fullmatch(
        r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name
    ):
        parser.error(
            "--name must start with a letter or digit and contain only letters, digits, '.', '_' or '-'"
        )

    if not args.skip_build:
        build()

    executable = find_executable()

    results_root = ROOT / "benchmark-results"
    results_root.mkdir(exist_ok=True)
    prefix = f"run-{args.name}-" if args.name else "run-"
    results_dir = Path(tempfile.mkdtemp(prefix=prefix, dir=results_root))
    csv_path = results_dir / "timings.csv"

    print(f"\nExecutable: {executable}")
    print(f"Results:    {results_dir}")

    reference_checksum = None
    rows = []

    with csv_path.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(
            [
                "run",
                "checksum_r",
                "checksum_g",
                "checksum_b",
                "scene_ms",
                "build_ms",
                "render_ms",
                "total_ms",
            ]
        )

        for trial in range(1, args.runs + 1):
            print(f"\n[{trial}/{args.runs}] Running...", flush=True)

            log_path = results_dir / f"trial-{trial:03d}.log"
            checksum, times = run_trial(executable, log_path)

            if reference_checksum is None:
                reference_checksum = checksum
            elif checksum != reference_checksum:
                raise RuntimeError(
                    f"Checksum mismatch on trial {trial}!\n"
                    f"Expected: {' '.join(reference_checksum)}\n"
                    f"Actual:   {' '.join(checksum)}\n"
                    f"See {log_path}"
                )

            rows.append(times)
            writer.writerow([trial, *checksum, *times])
            output.flush()

            print(
                f"Checksum OK | scene {times[0]:.3f} ms | "
                f"build {times[1]:.3f} ms | render {times[2]:.3f} ms | "
                f"total {times[3]:.3f} ms"
            )

    print(f"\nAll {args.runs} checksums match: {' '.join(reference_checksum)}")
    summarize(rows)
    print(f"\nRaw timings: {csv_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Error: {error}")
    except KeyboardInterrupt:
        raise SystemExit("\nBenchmark interrupted.")
