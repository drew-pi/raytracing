#!/usr/bin/env python3

import argparse
import csv
import math
import re
import statistics
import subprocess
import tempfile
from contextlib import nullcontext
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


def run_trial(executable, log_path, thread_count):
    with (
        log_path.open("wb") if log_path is not None else nullcontext(subprocess.PIPE)
    ) as log:
        result = subprocess.run(
            [str(executable), str(thread_count)],
            cwd=ROOT,
            stdout=subprocess.DEVNULL,
            stderr=log,
        )

    text = (
        log_path.read_text(errors="replace")
        if log_path is not None
        else result.stderr.decode(errors="replace")
    )
    details = (
        f"see {log_path}" if log_path is not None else f"benchmark output:\n{text}"
    )
    if result.returncode != 0:
        raise RuntimeError(f"Benchmark exited with code {result.returncode}; {details}")
    checksums = list(CHECKSUM_PATTERN.finditer(text))
    timings = list(TIMING_PATTERN.finditer(text))

    if len(checksums) != 1 or len(timings) != 1:
        raise RuntimeError(f"Expected exactly one checksum and timing row; {details}")

    checksum = checksums[0].groups()

    try:
        checksum_values = [float(value) for value in checksum]
        times = [float(value.strip()) for value in timings[0].group(1).split(",")]
    except ValueError as error:
        raise RuntimeError(f"Invalid numeric output; {details}") from error

    if not all(math.isfinite(value) for value in checksum_values):
        raise RuntimeError(f"Nonfinite checksum; {details}")

    if len(times) != 3 or not all(
        math.isfinite(value) and value >= 0 for value in times
    ):
        raise RuntimeError(f"Invalid phase timings; {details}")

    total = sum(times)
    if not math.isfinite(total):
        raise RuntimeError(f"Nonfinite total timing; {details}")

    return checksum, [*times, total]


def checksum_deviations(baseline, actual):
    deviations = []
    for component, expected, observed in zip("RGB", baseline, actual):
        expected_value, observed_value = float(expected), float(observed)
        if expected_value == observed_value:
            continue
        relative_error = (
            abs(observed_value - expected_value) / abs(expected_value)
            if expected_value != 0
            else math.inf
        )
        if relative_error >= 1e-4:
            deviations.append((component, expected, observed, relative_error * 100))
    return deviations


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
        "-t",
        "--threads",
        type=int,
        default=1,
        help="number of rendering workers (default: 1)",
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
    parser.add_argument(
        "--no-save",
        action="store_true",
        help="print results without saving CSV or logs (asks for confirmation)",
    )
    args = parser.parse_args()

    if args.runs < 1:
        parser.error("--runs must be at least 1")

    if not 1 <= args.threads <= 2_147_483_647:
        parser.error("--threads must be between 1 and 2147483647")

    if args.name is not None and not re.fullmatch(
        r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name
    ):
        parser.error(
            "--name must start with a letter or digit and contain only "
            "letters, digits, '.', '_' or '-'"
        )

    if args.no_save:
        while True:
            try:
                answer = (
                    input(
                        "Results and trial logs will NOT be saved. Are you sure? [y/N]: "
                    )
                    .strip()
                    .lower()
                )
            except EOFError:
                answer = "no"
            if answer in ("", "n", "no"):
                print("Benchmark cancelled.")
                return
            if answer in ("y", "yes"):
                break
            print("Please enter yes or no.")

    if not args.skip_build:
        build()

    executable = find_executable()

    results_dir = None
    csv_path = None
    if not args.no_save:
        results_root = ROOT / "benchmark-results"
        results_root.mkdir(exist_ok=True)
        prefix = f"run-{args.name}-" if args.name else "run-"
        results_dir = Path(tempfile.mkdtemp(prefix=prefix, dir=results_root))
        csv_path = results_dir / "timings.csv"

    print(f"\nExecutable: {executable}")
    print(f"Threads:    {args.threads}")
    print(
        f"Results:    {results_dir if results_dir is not None else 'console only (not saved)'}"
    )
    if args.name:
        print(f"Name:       {args.name}")

    if args.threads > 1:
        print(
            "\nNote: per-worker RNG streams make checksums scheduling-dependent.\n"
            "Checksum deviations only produce warnings. Finite checksums "
            "do not establish rendering correctness."
        )

    reference_checksum = None
    checksum_outside_tolerance = 0
    rows = []

    with (
        csv_path.open("w", newline="") if csv_path is not None else nullcontext()
    ) as output:
        writer = csv.writer(output) if output is not None else None
        if writer is not None:
            writer.writerow(
                [
                    "run",
                    "threads",
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

            log_path = (
                results_dir / f"trial-{trial:03d}.log"
                if results_dir is not None
                else None
            )
            checksum, times = run_trial(executable, log_path, args.threads)

            # Save every valid trial, including those outside checksum tolerance.
            rows.append(times)
            if writer is not None:
                writer.writerow([trial, args.threads, *checksum, *times])
                output.flush()
            else:
                print(f"Checksum: {' '.join(checksum)}")

            if reference_checksum is None:
                reference_checksum = checksum
            else:
                deviations = checksum_deviations(reference_checksum, checksum)
                if deviations:
                    checksum_outside_tolerance += 1
                    print(
                        f"Warning: checksum outside tolerance on trial {trial} "
                        "(each component must have relative deviation < 0.001%)."
                    )
                    for component, baseline, actual, percent_error in deviations:
                        print(
                            f"  {component}: baseline={baseline}, actual={actual}, "
                            f"percent error={percent_error:.9g}%"
                        )
                    print(
                        f"See {log_path}"
                        if log_path is not None
                        else "No trial log was saved."
                    )

            print(
                f"Trial complete | scene {times[0]:.3f} ms | "
                f"build {times[1]:.3f} ms | render {times[2]:.3f} ms | "
                f"total {times[3]:.3f} ms"
            )

    print(f"\nCompleted {args.runs} trials with {args.threads} workers.")
    print(f"Checksum baseline: first trial ({' '.join(reference_checksum)})")
    print(
        f"Trials outside checksum tolerance: {checksum_outside_tolerance} "
        f"of {args.runs} (per-component relative deviation must be < 0.001%; "
        "warnings only)."
    )

    summarize(rows)
    print("\nTotal is scene + build + render, not process wall-clock time.")
    if csv_path is not None:
        print(f"Raw timings: {csv_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Error: {error}")
    except KeyboardInterrupt:
        raise SystemExit("\nBenchmark interrupted.")
