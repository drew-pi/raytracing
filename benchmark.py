#!/usr/bin/env python3

import argparse
import csv
import json
import math
import re
import statistics
import subprocess
import tempfile
from contextlib import closing, nullcontext
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build" / "profile"

CHECKSUM_PATTERN = re.compile(r"checksum:\s*([^\s]+)\s+([^\s]+)\s+([^\s]+)")
TIMING_PATTERN = re.compile(r"scene_ms,build_ms,render_ms\s*\n([^\r\n]+)")


def build(full_pipeline=False):
    configure = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(BUILD),
        "-DCMAKE_CXX_COMPILER=g++",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON",
        "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O3 -g -DNDEBUG -march=native -fno-math-errno -fno-trapping-math",
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
            "raytracer_bench" if full_pipeline else "raytracer_bench_rendering",
            "--parallel",
        ],
        check=True,
    )


def find_executable(full_pipeline=False):
    target = "raytracer_bench" if full_pipeline else "raytracer_bench_rendering"
    # Support single-configuration and multi-configuration generators.
    candidates = [
        BUILD / "RelWithDebInfo" / target,
        BUILD / target,
    ]

    for candidate in candidates:
        if candidate.is_file():
            return candidate

    raise RuntimeError(f"Cannot find {target}; build it first.")


def query_metadata(executable):
    """Read settings compiled into the selected binary without loading assets."""
    try:
        result = subprocess.run(
            [str(executable), "--metadata"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(
            f"Cannot query benchmark metadata from {executable}; "
            "rebuild the selected executable without --skip-build."
        ) from error
    try:
        metadata = json.loads(result.stdout)
    except ValueError as error:
        raise RuntimeError(
            "Benchmark returned invalid metadata JSON; rebuild it."
        ) from error

    fields = ("scene", "scene_source", "width", "spp", "max_depth")
    if not isinstance(metadata, dict) or any(key not in metadata for key in fields):
        raise RuntimeError(
            "Benchmark metadata is missing scene or camera settings; rebuild it."
        )
    if any(
        not isinstance(metadata[key], str) or not metadata[key].strip()
        for key in ("scene", "scene_source")
    ) or any(
        type(metadata[key]) is not int or metadata[key] <= 0
        for key in ("width", "spp", "max_depth")
    ):
        raise RuntimeError("Benchmark returned invalid scene or camera settings.")
    return {key: metadata[key] for key in fields}


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


def run_render_session(executable, results_dir, runs, thread_count):
    """Stream validated records; keep stderr off a pipe to avoid deadlocks."""
    log_path = results_dir / "session.log" if results_dir is not None else None
    with (
        log_path.open("w") if log_path is not None else nullcontext() as log,
        tempfile.TemporaryFile() as diagnostics,
    ):
        process = subprocess.Popen(
            [str(executable), "--runs", str(runs), "--threads", str(thread_count)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=diagnostics,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        expected_run = 1
        startup_seen = False
        try:
            for line in process.stdout:
                if log is not None:
                    log.write(line)
                    log.flush()
                try:
                    record = json.loads(line)
                except ValueError as error:
                    raise RuntimeError("Malformed benchmark JSON line") from error
                if not isinstance(record, dict):
                    raise RuntimeError("Expected a benchmark JSON object")
                if not startup_seen:
                    if (
                        set(record) != {"type", "scene_ms", "build_ms"}
                        or record["type"] != "startup"
                    ):
                        raise RuntimeError(
                            "Expected exactly one startup row before renders"
                        )
                    values = [record["scene_ms"], record["build_ms"]]
                else:
                    if (
                        set(record) != {"type", "run", "render_ms", "checksum"}
                        or record["type"] != "render"
                        or type(record["run"]) is not int
                        or record["run"] != expected_run
                        or expected_run > runs
                    ):
                        raise RuntimeError(
                            f"Expected render row {expected_run} of {runs}; unexpected or trailing data"
                        )
                    checksum = record["checksum"]
                    if not isinstance(checksum, list) or len(checksum) != 3:
                        raise RuntimeError("Expected three checksum components")
                    values = [record["render_ms"], *checksum]
                try:
                    valid = all(
                        type(value) in (int, float)
                        and math.isfinite(value)
                        and value >= 0
                        for value in values
                    )
                except OverflowError:
                    valid = False
                if not valid:
                    raise RuntimeError(
                        "Expected finite nonnegative timings and checksum components"
                    )
                if not startup_seen:
                    startup_seen = True
                else:
                    expected_run += 1
                    if results_dir is not None:
                        (results_dir / f"trial-{record['run']:03d}.log").write_text(
                            line
                        )
                yield record
            returncode = process.wait()
            if returncode != 0:
                raise RuntimeError(f"Benchmark exited with code {returncode}")
            if not startup_seen or expected_run != runs + 1:
                raise RuntimeError(
                    f"Expected startup and {runs} render rows; received {expected_run - 1} renders"
                )
        except RuntimeError as error:
            details = (
                f"see {log_path}" if log_path is not None else "no session log saved"
            )
            raise RuntimeError(f"{error}; {details}") from error
        finally:
            # Also clean up when the caller fails while saving a yielded record.
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            if log is not None:
                for line in process.stdout:
                    log.write(line)
            process.stdout.close()
            diagnostics.seek(0)
            if log is not None:
                log.write("\n--- stderr ---\n")
                while chunk := diagnostics.read(65536):
                    log.write(chunk.decode("utf-8", errors="replace"))
            else:
                # No-save still exposes diagnostics, without retaining files.
                while chunk := diagnostics.read(65536):
                    print(chunk.decode("utf-8", errors="replace"), end="")


def run_full_pipeline(executable, results_dir, runs, thread_count):
    for trial in range(1, runs + 1):
        print(f"\n[{trial}/{runs}] Running...", flush=True)
        log_path = (
            results_dir / f"trial-{trial:03d}.log" if results_dir is not None else None
        )
        checksum, times = run_trial(executable, log_path, thread_count)
        yield {"type": "render", "run": trial, "checksum": checksum, "times": times}


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


def summarize(rows, full_pipeline=True):
    print("\nTimes in milliseconds:")
    print(
        f"{'Phase':<10} {'Mean':>12} {'Median':>12} "
        f"{'Min':>12} {'Max':>12} {'Stddev':>12}"
    )

    names = ("Scene", "Build", "Render", "Total") if full_pipeline else ("Render",)
    for column, name in enumerate(names):
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


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Build and benchmark rendering in one session (or the full pipeline per run)."
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
        "--full-pipeline",
        action="store_true",
        help="benchmark scene, build and render in an independent process per run",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="use the existing build/profile executable",
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
    args = parser.parse_args(argv)

    if not 1 <= args.runs <= 2_147_483_647:
        parser.error("--runs must be between 1 and 2147483647")

    if not 1 <= args.threads <= 2_147_483_647:
        parser.error("--threads must be between 1 and 2147483647")

    if args.name is not None and not re.fullmatch(
        r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name
    ):
        parser.error(
            "--name must start with a letter or digit and contain only "
            "letters, digits, '.', '_' or '-'"
        )

    return args


def main():
    args = parse_args()
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
        build(full_pipeline=args.full_pipeline)

    executable = find_executable(full_pipeline=args.full_pipeline)
    scene_metadata = query_metadata(executable)
    mode = "full-pipeline" if args.full_pipeline else "render-only"

    results_dir = None
    csv_path = None
    if not args.no_save:
        results_root = ROOT / "benchmark-results"
        results_root.mkdir(exist_ok=True)
        prefix = f"run-{args.name}-" if args.name else "run-"
        results_dir = Path(tempfile.mkdtemp(prefix=prefix, dir=results_root))
        csv_path = results_dir / "timings.csv"
        (results_dir / "metadata.json").write_text(
            json.dumps(
                {
                    "mode": mode,
                    "runs": args.runs,
                    "threads": args.threads,
                    "name": args.name,
                    "executable": str(executable),
                    **scene_metadata,
                },
                indent=2,
            )
            + "\n"
        )

    print(f"\nMode:       {mode}")
    print(f"Executable: {executable}")
    print(f"Threads:    {args.threads}")
    print(
        f"Scene:      {scene_metadata['scene']} | width {scene_metadata['width']} | "
        f"spp {scene_metadata['spp']} | depth {scene_metadata['max_depth']}"
    )
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
    startup = None
    records = (
        run_full_pipeline(executable, results_dir, args.runs, args.threads)
        if args.full_pipeline
        else run_render_session(executable, results_dir, args.runs, args.threads)
    )

    with (
        closing(records),
        (
            csv_path.open("w", newline="") if csv_path is not None else nullcontext()
        ) as output,
    ):
        writer = csv.writer(output) if output is not None else None
        if writer is not None:
            writer.writerow(
                [
                    "run",
                    "threads",
                    "checksum_r",
                    "checksum_g",
                    "checksum_b",
                ]
                + (
                    ["scene_ms", "build_ms", "render_ms", "total_ms"]
                    if args.full_pipeline
                    else ["render_ms"]
                )
            )
            output.flush()

        for record in records:
            if record["type"] == "startup":
                startup = record
                if results_dir is not None:
                    (results_dir / "startup.json").write_text(
                        json.dumps(startup, indent=2) + "\n"
                    )
                print(
                    f"Startup | scene {startup['scene_ms']:.3f} ms | "
                    f"build {startup['build_ms']:.3f} ms",
                    flush=True,
                )
                continue
            trial = record["run"]
            checksum = [str(value) for value in record["checksum"]]
            times = record["times"] if args.full_pipeline else [record["render_ms"]]
            log_path = (
                results_dir / f"trial-{trial:03d}.log"
                if results_dir is not None
                else None
            )

            # Preserve the failing trial before enforcing checksum consistency.
            rows.append(times)
            if writer is not None:
                writer.writerow([trial, args.threads, *checksum, *times])
                output.flush()
            else:
                print(f"Checksum: {' '.join(checksum)}")

            if reference_checksum is None:
                reference_checksum = checksum
            elif args.threads == 1:
                if any(
                    float(expected) != float(actual)
                    for expected, actual in zip(reference_checksum, checksum)
                ):
                    details = (
                        f"see {log_path}"
                        if log_path is not None
                        else "no trial log was saved"
                    )
                    raise RuntimeError(
                        f"Single-thread checksum mismatch on trial {trial}; "
                        "exact equality is required (no tolerance). "
                        f"Baseline: {' '.join(reference_checksum)}; "
                        f"actual: {' '.join(checksum)}; {details}"
                    )
            else:
                deviations = checksum_deviations(reference_checksum, checksum)
                if deviations:
                    checksum_outside_tolerance += 1
                    print(
                        f"Warning: checksum outside tolerance on trial {trial} "
                        "(each component must have relative deviation < 0.01%)."
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

            if args.full_pipeline:
                print(
                    f"Trial complete | scene {times[0]:.3f} ms | "
                    f"build {times[1]:.3f} ms | render {times[2]:.3f} ms | "
                    f"total {times[3]:.3f} ms"
                )
            else:
                print(
                    f"[{trial}/{args.runs}] Trial complete | render {times[0]:.3f} ms",
                    flush=True,
                )

    print(f"\nCompleted {args.runs} trials with {args.threads} workers.")
    print(f"Checksum baseline: first trial ({' '.join(reference_checksum)})")
    if args.threads == 1:
        print("All single-thread checksums match exactly (no tolerance).")
    else:
        print(
            f"Trials outside checksum tolerance: {checksum_outside_tolerance} "
            f"of {args.runs} (per-component relative deviation must be < 0.01%; "
            "warnings only)."
        )

    summarize(rows, full_pipeline=args.full_pipeline)
    if args.full_pipeline:
        print("\nTotal is scene + build + render, not process wall-clock time.")
    else:
        startup_ms = startup["scene_ms"] + startup["build_ms"]
        total_ms = startup_ms + sum(row[0] for row in rows)
        print(
            f"\nStartup (once): scene {startup['scene_ms']:.3f} ms + build {startup['build_ms']:.3f} ms = {startup_ms:.3f} ms"
        )
        print(
            f"Total measured session: {total_ms:.3f} ms (startup + all renders, not process wall-clock time)."
        )
    if csv_path is not None:
        print(f"Raw timings: {csv_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Error: {error}")
    except KeyboardInterrupt:
        raise SystemExit("\nBenchmark interrupted.")
