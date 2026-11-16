#!/usr/bin/env python3
"""Build, run, validate, and summarize BVH construction measurements."""

import argparse
import csv
import json
import math
import re
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build" / "construction"
TARGET = "raytracer_construction"
BUILD_TYPE = "RelWithDebInfo"
CXX_FLAGS = "-O3 -g -DNDEBUG -march=native -fno-math-errno -fno-trapping-math"
DEFAULT_SIZES = "1000,10000,100000,1000000"
CASE_FIELDS = {
    "type",
    "scene",
    "category",
    "requested_triangles",
    "triangle_count",
    "runs",
    "build_times_ms",
    "node_count",
    "internal_node_count",
    "leaf_count",
    "maximum_depth",
    "average_leaf_depth",
    "average_primitives_per_leaf",
    "maximum_primitives_in_leaf",
    "estimated_sah_cost",
    "node_bytes",
    "index_bytes",
}
INTEGER_FIELDS = (
    "triangle_count",
    "runs",
    "node_count",
    "internal_node_count",
    "leaf_count",
    "maximum_depth",
    "maximum_primitives_in_leaf",
    "node_bytes",
    "index_bytes",
)
FLOAT_FIELDS = (
    "average_leaf_depth",
    "average_primitives_per_leaf",
    "estimated_sah_cost",
)
CSV_FIELDS: tuple[str, ...] = (
    "scene",
    "category",
    "requested_triangles",
    "triangle_count",
    "runs",
    "mean_build_ms",
    "median_build_ms",
    "min_build_ms",
    "max_build_ms",
    "stddev_build_ms",
    "node_count",
    "internal_node_count",
    "leaf_count",
    "maximum_depth",
    "average_leaf_depth",
    "average_primitives_per_leaf",
    "maximum_primitives_in_leaf",
    "estimated_sah_cost",
    "node_bytes",
    "index_bytes",
)


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def git_output(*arguments):
    return subprocess.run(
        ["git", "--no-pager", "--no-optional-locks", *arguments],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
        timeout=10,
    ).stdout.rstrip("\n")


def parse_sizes(text):
    parts = text.split(",")
    if not parts or any(not part.strip() for part in parts):
        raise argparse.ArgumentTypeError(
            "sizes must be a comma-separated list of positive integers"
        )
    try:
        sizes = [int(part) for part in parts]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "sizes must be a comma-separated list of positive integers"
        ) from error
    if any(size <= 0 for size in sizes):
        raise argparse.ArgumentTypeError("sizes must be positive")
    if len(set(sizes)) != len(sizes):
        raise argparse.ArgumentTypeError("sizes must not contain duplicates")
    return sizes


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
        log.write("Command: " + " ".join(command) + "\n")
        log.flush()
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=log, check=True)


def find_executable():
    for candidate in (BUILD / BUILD_TYPE / TARGET, BUILD / TARGET):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Cannot find {TARGET} in {BUILD}; run without --skip-build.")


def run_command(command, log, timeout=None):
    log.write("Command: " + " ".join(command) + "\n")
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
        check=False,
    )
    log.write(result.stdout)
    log.flush()
    result.check_returncode()
    return result.stdout


def validate_metadata(value):
    if not isinstance(value, dict):
        raise ValueError("metadata must be a JSON object")
    if not isinstance(value.get("suite"), str) or not value["suite"].strip():
        raise ValueError("metadata suite must be a nonempty string")
    return value


def finite_nonnegative(value):
    if type(value) not in (int, float) or value < 0:
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


def validate_case(record, runs, requested_sizes, line_number):
    prefix = f"line {line_number}"
    if set(record) != CASE_FIELDS:
        missing = sorted(CASE_FIELDS - set(record))
        extra = sorted(set(record) - CASE_FIELDS)
        raise ValueError(
            f"{prefix}: construction fields differ (missing={missing}, extra={extra})"
        )
    if record["type"] != "construction":
        raise ValueError(f"{prefix}: expected construction record")
    for field in ("scene", "category"):
        if not isinstance(record[field], str) or not record[field].strip():
            raise ValueError(f"{prefix}: {field} must be a nonempty string")
    requested = record["requested_triangles"]
    if requested is None:
        if "dragon" not in record["scene"].casefold():
            raise ValueError(
                f"{prefix}: requested_triangles may be null only for dragon"
            )
    elif (
        type(requested) is not int or requested <= 0 or requested not in requested_sizes
    ):
        raise ValueError(
            f"{prefix}: requested_triangles must be one of the requested sizes"
        )
    for field in INTEGER_FIELDS:
        if type(record[field]) is not int or record[field] < 0:
            raise ValueError(f"{prefix}: {field} must be a nonnegative integer")
    if record["runs"] != runs:
        raise ValueError(f"{prefix}: runs must equal requested run count {runs}")
    times = record["build_times_ms"]
    if (
        not isinstance(times, list)
        or len(times) != runs
        or not all(finite_nonnegative(value) for value in times)
    ):
        raise ValueError(
            f"{prefix}: build_times_ms must contain {runs} finite nonnegative numbers"
        )
    for field in FLOAT_FIELDS:
        if not finite_nonnegative(record[field]):
            raise ValueError(f"{prefix}: {field} must be a finite nonnegative number")
    nodes = record["node_count"]
    internal = record["internal_node_count"]
    leaves = record["leaf_count"]
    triangles = record["triangle_count"]
    if requested is not None and triangles != requested:
        raise ValueError(
            f"{prefix}: generated triangle_count must equal requested_triangles"
        )
    if triangles <= 0 or nodes <= 0 or leaves <= 0:
        raise ValueError(
            f"{prefix}: triangle_count, node_count, and leaf_count must be positive"
        )
    if nodes != internal + leaves:
        raise ValueError(
            f"{prefix}: node_count must equal internal_node_count + leaf_count"
        )
    if record["maximum_depth"] < record["average_leaf_depth"]:
        raise ValueError(f"{prefix}: average_leaf_depth exceeds maximum_depth")
    average = record["average_primitives_per_leaf"]
    maximum = record["maximum_primitives_in_leaf"]
    if average <= 0 or maximum <= 0 or average > maximum:
        raise ValueError(f"{prefix}: invalid leaf primitive statistics")
    expected_average = triangles / leaves
    if not math.isclose(average, expected_average, rel_tol=1e-6, abs_tol=1e-9):
        raise ValueError(
            f"{prefix}: average_primitives_per_leaf does not equal triangle_count / leaf_count"
        )
    return record


def validate_records(text, runs, requested_sizes):
    cases = []
    complete = False
    identities = set()
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except ValueError as error:
            raise ValueError(f"line {line_number}: invalid JSON") from error
        if not isinstance(record, dict):
            raise ValueError(f"line {line_number}: expected a JSON object")
        if complete:
            raise ValueError(f"line {line_number}: trailing record after complete")
        if record.get("type") == "complete":
            if set(record) != {"type"}:
                raise ValueError(
                    f"line {line_number}: complete record must contain only type"
                )
            complete = True
            continue
        case = validate_case(record, runs, set(requested_sizes), line_number)
        identity = (case["scene"], case["category"], case["requested_triangles"])
        if identity in identities:
            raise ValueError(
                f"line {line_number}: duplicate construction case {identity!r}"
            )
        identities.add(identity)
        cases.append(case)
    if not complete:
        raise ValueError("missing final complete record")
    if not cases:
        raise ValueError("no construction records")
    generated_scenes = {
        "uniform",
        "clustered",
        "coplanar",
        "identical-centroids",
        "long-thin",
    }
    expected = {(scene, size) for scene in generated_scenes for size in requested_sizes}
    observed = {
        (case["scene"], case["requested_triangles"])
        for case in cases
        if case["requested_triangles"] is not None
    }
    if observed != expected:
        raise ValueError(
            f"construction suite differs (missing={sorted(expected - observed)}, "
            f"unexpected={sorted(observed - expected)})"
        )
    dragon = [case for case in cases if case["requested_triangles"] is None]
    if len(dragon) != 1 or dragon[0]["scene"] != "dragon":
        raise ValueError("expected exactly one dragon construction record")
    return cases


def summarized_row(case):
    times = case["build_times_ms"]
    return {
        **{field: case[field] for field in CSV_FIELDS if field in case},
        "requested_triangles": ""
        if case["requested_triangles"] is None
        else case["requested_triangles"],
        "mean_build_ms": statistics.fmean(times),
        "median_build_ms": statistics.median(times),
        "min_build_ms": min(times),
        "max_build_ms": max(times),
        "stddev_build_ms": statistics.stdev(times) if len(times) > 1 else 0.0,
    }


def format_table(rows):
    table = [
        [
            "scene",
            "requested",
            "triangles",
            "mean ms",
            "median ms",
            "nodes",
            "leaves",
            "depth",
        ]
    ]
    for case, row in rows:
        table.append(
            [
                case["scene"],
                "—"
                if case["requested_triangles"] is None
                else str(case["requested_triangles"]),
                str(case["triangle_count"]),
                f"{row['mean_build_ms']:.3f}",
                f"{row['median_build_ms']:.3f}",
                str(case["node_count"]),
                str(case["leaf_count"]),
                str(case["maximum_depth"]),
            ]
        )
    widths = [max(len(row[index]) for row in table) for index in range(len(table[0]))]
    return "\n".join(
        "  ".join(cell.ljust(width) for cell, width in zip(row, widths))
        for row in table
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-build", action="store_true", help="reuse the existing executable"
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="new directory for analysis output (default: benchmark-results/construction-[name-]<timestamp>)",
    )
    parser.add_argument(
        "--name",
        help="optional label used in the default output directory name",
    )
    parser.add_argument(
        "--runs", type=int, default=3, help="build repetitions per case (default: 3)"
    )
    parser.add_argument(
        "--sizes",
        type=parse_sizes,
        default=parse_sizes(DEFAULT_SIZES),
        help=f"comma-separated triangle counts (default: {DEFAULT_SIZES})",
    )
    args = parser.parse_args(argv)
    if not 1 <= args.runs <= 2_147_483_647:
        parser.error("--runs must be between 1 and 2147483647")
    if args.name is not None and not re.fullmatch(
        r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name
    ):
        parser.error(
            "--name must start with a letter or digit and contain only "
            "letters, digits, '.', '_' or '-'"
        )

    if args.output_dir is None:
        suffix = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        directory_name = (
            f"construction-{args.name}-{suffix}"
            if args.name
            else f"construction-{suffix}"
        )
        output = ROOT / "benchmark-results" / directory_name
    else:
        output = args.output_dir.expanduser().resolve()
    output.mkdir(parents=True)
    log_path = output / "construction.log"
    started = timestamp()
    commit = git_output("rev-parse", "HEAD")
    git_status = git_output("status", "--porcelain=v1", "--untracked-files=normal")

    try:
        with log_path.open("w", encoding="utf-8") as log:
            if not args.skip_build:
                build(log)
            executable = find_executable()
            metadata_text = run_command(
                [str(executable), "--metadata"], log, timeout=10
            )
            try:
                executable_metadata = validate_metadata(json.loads(metadata_text))
            except ValueError as error:
                raise RuntimeError(
                    f"Invalid construction metadata: {error}; rebuild the executable."
                ) from error
            command = [
                str(executable),
                "--runs",
                str(args.runs),
                "--sizes",
                ",".join(map(str, args.sizes)),
            ]
            raw_text = run_command(command, log)
        try:
            cases = validate_records(raw_text, args.runs, args.sizes)
        except ValueError as error:
            raise RuntimeError(
                f"Invalid construction output: {error}; see {log_path}"
            ) from error

        rows = [(case, summarized_row(case)) for case in cases]
        raw = {
            "metadata": executable_metadata,
            "records": [*cases, {"type": "complete"}],
        }
        (output / "raw.json").write_text(
            json.dumps(raw, indent=2) + "\n", encoding="utf-8"
        )
        with (output / "construction.csv").open(
            "w", newline="", encoding="utf-8"
        ) as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
            writer.writeheader()
            writer.writerows(row for _, row in rows)
        table = format_table(rows)
        summary = f"Construction analysis\nSuite: {executable_metadata['suite']}\nRuns per case: {args.runs}\nSizes: {','.join(map(str, args.sizes))}\n\n{table}\n"
        (output / "summary.txt").write_text(summary, encoding="utf-8")
        metadata = {
            "suite": executable_metadata["suite"],
            "name": args.name,
            "started_at": started,
            "finished_at": timestamp(),
            "runs": args.runs,
            "sizes": args.sizes,
            "executable": str(executable),
            "command": command,
            "git": {"commit": commit, "dirty": bool(git_status), "status": git_status},
            "case_count": len(cases),
        }
        (output / "metadata.json").write_text(
            json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
        )
        print(table)
        print(f"\nResults: {output}")
    except Exception:
        print(f"Analysis failed; partial output is in {output}", file=sys.stderr)
        raise


if __name__ == "__main__":
    main()
