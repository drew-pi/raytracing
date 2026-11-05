#!/usr/bin/env python3
"""Run one single-threaded diagnostic render and summarize traversal counters."""

import argparse
import csv
import json
import math
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build" / "diagnostic"
TARGET = "raytracer_diagnostic"
BUILD_TYPE = "RelWithDebInfo"
CXX_FLAGS = "-O3 -g -DNDEBUG -march=native -fno-math-errno -fno-trapping-math"
COUNTERS = (
    "rays",
    "box_tests",
    "box_hits",
    "leaf_visits",
    "triangle_tests",
    "triangle_hits",
)


def build(log):
    configure = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(BUILD),
        "-DCMAKE_CXX_COMPILER=g++",
        f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
        "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON",
        f"-DCMAKE_CXX_FLAGS_RELWITHDEBINFO={CXX_FLAGS}",
    ]
    # Match benchmark.py: changing compilers can reset the existing cache.
    commands = [
        configure,
        configure,
        [
            "cmake",
            "--build",
            str(BUILD),
            "--config",
            BUILD_TYPE,
            "--target",
            TARGET,
            "--parallel",
        ],
    ]
    for command in commands:
        log.write("\nCommand: " + " ".join(command) + "\n")
        log.flush()
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=log, check=True)


def find_executable():
    for candidate in (BUILD / BUILD_TYPE / TARGET, BUILD / TARGET):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Cannot find {TARGET} in {BUILD}; run without --skip-build.")


def run_command(command, log, timeout=None):
    log.write("\nCommand: " + " ".join(command) + "\n")
    log.flush()
    result = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=log,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )
    log.write(result.stdout)
    log.flush()
    result.check_returncode()
    return result.stdout


def validate_metadata(metadata):
    if not isinstance(metadata, dict):
        raise ValueError("metadata must be a JSON object")
    if not isinstance(metadata.get("scene"), str) or not metadata["scene"].strip():
        raise ValueError("metadata must contain a nonempty scene name")
    for key in ("width", "spp", "max_depth"):
        if type(metadata.get(key)) is not int or metadata[key] <= 0:
            raise ValueError(f"metadata {key} must be a positive integer")
    return metadata


def query_metadata(executable, log):
    text = run_command([str(executable), "--metadata"], log, timeout=10)
    try:
        return validate_metadata(json.loads(text))
    except ValueError as error:
        raise RuntimeError(
            f"Invalid diagnostic metadata: {error}; rebuild the executable."
        ) from error


def validate_records(text, max_depth):
    """Validate zero-based bounce rows; depth-limited rays do not traverse the BVH."""
    rows = {}
    checksum = None
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except ValueError as error:
            raise ValueError(f"line {line_number}: invalid JSON") from error
        if not isinstance(record, dict):
            raise ValueError(f"line {line_number}: expected a JSON object")
        kind = record.get("type")
        if kind == "render":
            if checksum is not None:
                raise ValueError("expected exactly one render record, found duplicates")
            checksum = record.get("checksum")
            if (
                not isinstance(checksum, list)
                or len(checksum) != 3
                or any(type(value) not in (int, float) for value in checksum)
            ):
                raise ValueError("render checksum must contain three finite numbers")
            try:
                finite = all(math.isfinite(value) for value in checksum)
            except OverflowError:
                finite = False
            if not finite:
                raise ValueError("render checksum must contain three finite numbers")
        elif kind == "depth":
            for key in ("depth", *COUNTERS):
                if type(record.get(key)) is not int or record[key] < 0:
                    raise ValueError(
                        f"line {line_number}: {key} must be a nonnegative integer"
                    )
            depth = record["depth"]
            if depth in rows:
                raise ValueError(f"duplicate depth row {depth}")
            for hits, tests in (
                ("box_hits", "box_tests"),
                ("triangle_hits", "triangle_tests"),
            ):
                if record[hits] > record[tests]:
                    raise ValueError(f"depth {depth}: {hits} exceeds {tests}")
            if record["leaf_visits"] > record["box_hits"]:
                raise ValueError(f"depth {depth}: leaf_visits exceeds box_hits")
            if record["rays"] == 0 and any(record[key] for key in COUNTERS[1:]):
                raise ValueError(
                    f"depth {depth}: traversal counters are nonzero with zero rays"
                )
            rows[depth] = {key: record[key] for key in ("depth", *COUNTERS)}
        else:
            raise ValueError(f"line {line_number}: unknown record type {kind!r}")
    if checksum is None:
        raise ValueError("expected exactly one render record, found none")
    expected = set(range(max_depth))
    if set(rows) != expected:
        raise ValueError(
            f"expected depth rows 0 through {max_depth - 1}; "
            f"missing {sorted(expected - set(rows))}, unexpected {sorted(set(rows) - expected)}"
        )
    ordered = [rows[depth] for depth in range(max_depth)]
    for previous, current in zip(ordered, ordered[1:]):
        if current["rays"] > previous["rays"]:
            raise ValueError(
                f"depth {current['depth']}: ray count increases from previous depth"
            )
    return checksum, ordered


def format_table(rows):
    def ratio(numerator, denominator, scale=1):
        return f"{scale * numerator / denominator:.2f}" if denominator else "—"

    def cells(label, row):
        return [
            str(label),
            str(row["rays"]),
            ratio(row["box_tests"], row["rays"]),
            ratio(row["box_hits"], row["box_tests"], 100),
            ratio(row["leaf_visits"], row["rays"]),
            ratio(row["triangle_tests"], row["rays"]),
        ]

    totals = {key: sum(row[key] for row in rows) for key in COUNTERS}
    table = [["depth", "rays", "boxes/ray", "box-hit%", "leaves/ray", "triangles/ray"]]
    table.extend(cells(row["depth"], row) for row in rows)
    table.append(cells("total", totals))
    widths = [max(len(row[column]) for row in table) for column in range(len(table[0]))]
    return "\n".join(
        "  ".join(cell.rjust(width) for cell, width in zip(row, widths))
        for row in table
    )


def git_info():
    info = {"revision": None, "dirty": None, "status": None}
    try:
        for key, arguments in (
            ("revision", ["rev-parse", "HEAD"]),
            (
                "status",
                [
                    "--no-optional-locks",
                    "status",
                    "--porcelain",
                    "--untracked-files=normal",
                ],
            ),
        ):
            result = subprocess.run(
                ["git", "--no-pager", *arguments],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=True,
                timeout=10,
            )
            info[key] = result.stdout.strip() if key == "revision" else result.stdout
        info["dirty"] = bool(info["status"])
    except (OSError, subprocess.SubprocessError) as error:
        info["error"] = str(error)
    return info


def write_metadata(path, metadata):
    path.write_text(
        json.dumps(metadata, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="use the existing diagnostic executable",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="save results in this new directory (must not already exist)",
    )
    args = parser.parse_args(argv)
    results_dir = None
    try:
        git = git_info()
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        results_dir = (
            args.output_dir.expanduser().resolve()
            if args.output_dir is not None
            else ROOT / "benchmark-results" / f"diagnostic-{timestamp}"
        )
        results_dir.mkdir(parents=True, exist_ok=False)
        print(f"Results: {results_dir}", flush=True)
        metadata_path = results_dir / "metadata.json"
        metadata = {
            "timestamp": timestamp,
            "threads": 1,
            "runs": 1,
            "checksum": None,
            "git": git,
            "build": {
                "directory": str(BUILD),
                "compiler": "g++",
                "type": BUILD_TYPE,
                "lto": True,
                "cxx_flags": CXX_FLAGS,
                "target": TARGET,
                "skipped": args.skip_build,
                "settings_source": "requested (existing binary not verified with --skip-build)",
            },
        }
        write_metadata(metadata_path, metadata)
        with (results_dir / "diagnostic.log").open("w", encoding="utf-8") as log:
            if not args.skip_build:
                print(f"Building {TARGET}...", flush=True)
                build(log)
            executable = find_executable()
            metadata["executable"] = str(executable)
            metadata.update(query_metadata(executable, log))
            # Keep runner settings authoritative over any executable metadata.
            metadata["threads"] = 1
            metadata["runs"] = 1
            write_metadata(metadata_path, metadata)
            print("Running one single-threaded diagnostic render...", flush=True)
            output = run_command([str(executable)], log)
        try:
            checksum, rows = validate_records(output, metadata["max_depth"])
        except ValueError as error:
            raise RuntimeError(f"Invalid diagnostic output: {error}") from error
        metadata["checksum"] = checksum
        write_metadata(metadata_path, metadata)
        with (results_dir / "counters.csv").open(
            "w", newline="", encoding="utf-8"
        ) as output_file:
            writer = csv.DictWriter(output_file, fieldnames=["depth", *COUNTERS])
            writer.writeheader()
            writer.writerows(rows)
        summary = (
            f"Scene: {metadata['scene']} | width {metadata['width']} | "
            f"spp {metadata['spp']} | depth {metadata['max_depth']}\n"
            f"Threads: 1\nChecksum: {json.dumps(checksum)}\n\n{format_table(rows)}\n"
        )
        (results_dir / "summary.txt").write_text(summary, encoding="utf-8")
        print(summary, end="")
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        details = f"; see {results_dir / 'diagnostic.log'}" if results_dir else ""
        print(f"Diagnostic failed: {error}{details}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.returncode > 0:
            return error.returncode
        return 1


if __name__ == "__main__":
    sys.exit(main())
