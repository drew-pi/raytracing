#!/usr/bin/env python3
"""Run renderer analysis, optionally followed by BVH construction analysis."""

import argparse
import csv
import hashlib
import json
import math
import re
import shlex
import shutil
import statistics
import subprocess
import sys
from datetime import date, datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent


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


def read_checksum(values, source):
    if not isinstance(values, (list, tuple)) or len(values) != 3:
        raise RuntimeError(f"{source}: expected three checksum components")
    try:
        if any(isinstance(value, bool) for value in values):
            raise ValueError("boolean checksum")
        checksum = [float(value) for value in values]
        if not all(math.isfinite(value) for value in checksum):
            raise ValueError("non-finite checksum")
    except (TypeError, ValueError, OverflowError) as error:
        raise RuntimeError(f"{source}: invalid checksum {values!r}") from error
    return checksum


def read_stage_results(name, directory, runs, full_pipeline):
    try:
        metadata = json.loads((directory / "metadata.json").read_text())
        if not isinstance(metadata, dict):
            raise ValueError("metadata must be an object")
        if type(metadata.get("threads")) is not int or metadata["threads"] != 1:
            raise ValueError("exact checksum validation requires one render thread")
        expected_runs = runs if name == "benchmark" else 1
        if type(metadata.get("runs")) is not int or metadata["runs"] != expected_runs:
            raise ValueError(f"expected metadata for {expected_runs} runs")
        workload = {
            key: metadata[key]
            for key in ("scene", "scene_source", "width", "spp", "max_depth")
        }
        if any(
            not isinstance(workload[key], str) or not workload[key].strip()
            for key in ("scene", "scene_source")
        ) or any(
            type(workload[key]) is not int or workload[key] <= 0
            for key in ("width", "spp", "max_depth")
        ):
            raise ValueError("invalid scene or camera metadata")

        if name == "benchmark":
            expected_mode = "full-pipeline" if full_pipeline else "render-only"
            if metadata.get("mode") != expected_mode:
                raise ValueError(f"expected benchmark mode {expected_mode}")
            with (directory / "timings.csv").open(newline="") as output:
                rows = list(csv.DictReader(output))
            if len(rows) != runs:
                raise ValueError(f"expected {runs} benchmark checksum rows")
            checksums = []
            for trial, row in enumerate(rows, 1):
                if int(row["run"]) != trial or int(row["threads"]) != 1:
                    raise ValueError(
                        f"invalid run/thread count in benchmark row {trial}"
                    )
                checksums.append(
                    read_checksum(
                        [row[f"checksum_{channel}"] for channel in "rgb"],
                        f"benchmark trial {trial}",
                    )
                )
        elif name == "diagnostic":
            checksums = [read_checksum(metadata.get("checksum"), "diagnostic")]
        else:
            expected_mode = "full-pipeline" if full_pipeline else "render-only"
            if metadata.get("mode") != expected_mode:
                raise ValueError(f"expected profile mode {expected_mode}")
            text = (directory / "benchmark.log").read_text(errors="replace")
            if full_pipeline:
                records = re.findall(
                    r"^checksum:\s*([^\s]+)\s+([^\s]+)\s+([^\s]+)[ \t]*$",
                    text,
                    flags=re.MULTILINE,
                )
                checksums = [read_checksum(record, "profile") for record in records]
            else:
                records = [
                    json.loads(line)
                    for line in text.splitlines()
                    if line.lstrip().startswith("{")
                ]
                renders = [
                    record
                    for record in records
                    if isinstance(record, dict) and record.get("type") == "render"
                ]
                if any(
                    type(record.get("run")) is not int or record["run"] != 1
                    for record in renders
                ):
                    raise ValueError("expected profile render trial 1")
                checksums = [
                    read_checksum(record.get("checksum"), "profile")
                    for record in renders
                ]
            if len(checksums) != 1:
                raise ValueError("expected exactly one profile render checksum")
        return {"workload": workload, "checksums": checksums}
    except (OSError, ValueError, TypeError, KeyError) as error:
        raise RuntimeError(
            f"Cannot validate {name} results in {directory}: {error}"
        ) from error


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as binary:
        for chunk in iter(lambda: binary.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def capture_benchmark_binary(directory):
    try:
        metadata = json.loads((directory / "metadata.json").read_text())
        executable = metadata["executable"]
        if not isinstance(executable, str) or not executable:
            raise ValueError("missing executable path")
        executable = Path(executable).expanduser()
        if not executable.is_absolute():
            executable = ROOT / executable
        executable = executable.resolve(strict=True)
        return {"executable": str(executable), "sha256": file_sha256(executable)}
    except (OSError, ValueError, TypeError, KeyError) as error:
        raise RuntimeError(
            f"Cannot identify the benchmark executable: {error}"
        ) from error


def write_assembly_dump(assembly):
    executable = Path(assembly["executable"])
    assembly["command"] = [assembly["tool"], "-d", "-C", "-l", str(executable)]
    try:
        if file_sha256(executable) != assembly["sha256"]:
            raise RuntimeError(
                "The benchmark executable changed after benchmarking; refusing to dump a different binary"
            )
        with (
            Path(assembly["path"]).open("wb") as output,
            Path(assembly["log"]).open("wb") as log,
        ):
            subprocess.run(
                assembly["command"],
                cwd=ROOT,
                stdout=output,
                stderr=log,
                check=True,
                timeout=120,
            )
        if file_sha256(executable) != assembly["sha256"]:
            raise RuntimeError("The benchmark executable changed during disassembly")
        if Path(assembly["path"]).stat().st_size == 0:
            raise RuntimeError("The disassembler produced an empty dump")
        assembly["status"] = "completed"
    except KeyboardInterrupt:
        assembly["status"] = "interrupted"
        raise
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        assembly.update(status="failed", error=str(error))
        raise RuntimeError(
            f"Assembly dump failed: {error}; details: {assembly['log']}"
        ) from error


def collect_summary(results, validation, runs, full_pipeline, focus):
    def milliseconds(value):
        if isinstance(value, bool):
            raise ValueError("boolean timing")
        value = float(value)
        if not math.isfinite(value) or value < 0:
            raise ValueError("timings must be finite and nonnegative")
        return value

    try:
        with (results / "benchmark" / "timings.csv").open(newline="") as output:
            timings = list(csv.DictReader(output))
        if len(timings) != runs:
            raise ValueError(f"expected {runs} benchmark timing rows")
        render_times = [milliseconds(row["render_ms"]) for row in timings]
        if full_pipeline:
            build_ms = statistics.mean(milliseconds(row["build_ms"]) for row in timings)
        else:
            startup = json.loads((results / "benchmark" / "startup.json").read_text())
            build_ms = milliseconds(startup["build_ms"])

        with (results / "diagnostic" / "counters.csv").open(newline="") as output:
            counters = list(csv.DictReader(output))
        if [int(row["depth"]) for row in counters] != list(
            range(validation["workload"]["max_depth"])
        ):
            raise ValueError("expected one diagnostic counter row per bounce depth")
        totals = {}
        for key in ("box_tests", "triangle_tests"):
            values = [int(row[key]) for row in counters]
            if any(value < 0 for value in values):
                raise ValueError(f"negative diagnostic {key}")
            totals[key] = sum(values)

        summary = {
            "focus": focus,
            "render_mean_ms": statistics.mean(render_times),
            "render_stddev_ms": statistics.stdev(render_times) if runs > 1 else None,
            "render_runs": runs,
            "checksum": validation["baseline"],
            **totals,
            "build_time_ms": build_ms,
            "build_time_kind": "mean_per_trial" if full_pipeline else "one_time",
        }
        if focus == "full":
            with (results / "construction" / "construction.csv").open(
                newline=""
            ) as output:
                construction = list(csv.DictReader(output))
            if not construction:
                raise ValueError("construction analysis has no cases")
            summary["construction_cases"] = len(construction)
            summary["construction_results"] = str(
                results / "construction" / "construction.csv"
            )
        return summary
    except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
        raise RuntimeError(f"Cannot summarize run results: {error}") from error


def format_summary(summary):
    deviation = summary["render_stddev_ms"]
    deviation = (
        f"{deviation:.3f} ms (sample)"
        if deviation is not None
        else "N/A (one measured render)"
    )
    build_kind = (
        "mean across benchmark trials"
        if summary["build_time_kind"] == "mean_per_trial"
        else "one-time benchmark scene/BVH build"
    )
    return (
        "Run summary\n"
        f"Mean render time: {summary['render_mean_ms']:.3f} ms ({summary['render_runs']} runs)\n"
        f"Render stddev:   {deviation}\n"
        f"Checksum:        {json.dumps(summary['checksum'])} (exact match)\n"
        f"Box tests:       {summary['box_tests']:,}\n"
        f"Triangle tests:  {summary['triangle_tests']:,}\n"
        f"Build time:      {summary['build_time_ms']:.3f} ms ({build_kind})\n"
        "Traversal counts are for one diagnostic render, summed over all bounce depths.\n"
        + (
            f"Construction cases: {summary['construction_cases']} "
            f"({summary['construction_results']})\n"
            if summary["focus"] == "full"
            else ""
        )
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True, help="descriptive run name")
    parser.add_argument(
        "--focus",
        choices=("renderer", "full"),
        default="renderer",
        help="renderer runs the existing workflow; full additionally analyzes BVH construction",
    )
    parser.add_argument(
        "--date",
        type=date.fromisoformat,
        default=date.today(),
        help="run date in YYYY-MM-DD format (default: today)",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
        help="measured benchmark renders (default: 5; diagnostic/profile each render once)",
    )
    parser.add_argument(
        "--skip-build", action="store_true", help="reuse all existing executables"
    )
    parser.add_argument(
        "--construction-runs",
        type=int,
        default=3,
        help="BVH build repetitions per construction case in full focus (default: 3)",
    )
    parser.add_argument(
        "--construction-sizes",
        default="1000,10000,100000,1000000",
        help="comma-separated synthetic triangle counts in full focus",
    )
    parser.add_argument(
        "--full-pipeline",
        action="store_true",
        help="benchmark/profile the full pipeline instead of rendering only",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / "benchmark-results",
        help="parent results directory (default: benchmark-results/)",
    )
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name):
        parser.error(
            "--name must start with a letter or digit and use only letters, digits, '.', '_' or '-'"
        )
    if not 1 <= args.runs <= 2_147_483_647:
        parser.error("--runs must be between 1 and 2147483647")
    if not 1 <= args.construction_runs <= 2_147_483_647:
        parser.error("--construction-runs must be between 1 and 2147483647")
    if not re.fullmatch(r"[1-9][0-9]*(,[1-9][0-9]*)*", args.construction_sizes):
        parser.error("--construction-sizes must be comma-separated positive integers")

    disassembler = shutil.which("objdump") or shutil.which("llvm-objdump")
    if disassembler is None:
        raise RuntimeError(
            "Assembly dumping requires objdump (GNU binutils) or llvm-objdump on PATH"
        )

    commit = git_output("rev-parse", "HEAD")
    git_status = git_output("status", "--porcelain=v1", "--untracked-files=normal")
    results = (
        args.output_root.expanduser().resolve()
        / f"{args.date.isoformat()}_{args.name}_{commit[:12]}"
    )
    results.parent.mkdir(parents=True, exist_ok=True)
    # Refuse to overwrite any earlier run with the same name/date/commit.
    results.mkdir()

    metadata = {
        "name": args.name,
        "focus": args.focus,
        "date": args.date.isoformat(),
        "git": {"commit": commit, "dirty": bool(git_status), "status": git_status},
        "started_at": timestamp(),
        "finished_at": None,
        "status": "running",
        "assembly_dump": {
            "status": "pending",
            "tool": disassembler,
            "path": str(results / "benchmark.asm"),
            "log": str(results / "assembly.log"),
        },
        "checksum_validation": {
            "status": "pending",
            "comparison": "exact",
            "threads": 1,
            "baseline": None,
            "workload": None,
            "stages": {},
        },
        "stages": [],
    }
    for name in ("benchmark", "diagnostic", "profile"):
        command = [
            sys.executable,
            str(ROOT / f"{name}.py"),
            "--output-dir",
            str(results / name),
        ]
        if args.skip_build:
            command.append("--skip-build")
        if name == "benchmark":
            command.extend(
                ["--runs", str(args.runs), "--threads", "1", "--name", args.name]
            )
        if name == "profile":
            command.append("--no-serve")
        if args.full_pipeline and name != "diagnostic":
            command.append("--full-pipeline")
        metadata["stages"].append(
            {
                "name": name,
                "command": command,
                "status": "pending",
                "output_directory": str(results / name),
                "log": str(results / f"{name}.log"),
            }
        )
    if args.focus == "full":
        command = [
            sys.executable,
            str(ROOT / "construction_analysis.py"),
            "--output-dir",
            str(results / "construction"),
            "--runs",
            str(args.construction_runs),
            "--sizes",
            args.construction_sizes,
        ]
        if args.skip_build:
            command.append("--skip-build")
        metadata["stages"].append(
            {
                "name": "construction",
                "command": command,
                "status": "pending",
                "output_directory": str(results / "construction"),
                "log": str(results / "construction.log"),
            }
        )

    def save():
        (results / "metadata.json").write_text(
            json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
        )

    save()
    print(f"Results: {results}\nGit commit: {commit}", flush=True)
    if git_status:
        print(
            "Warning: the working tree has uncommitted changes; status is recorded in metadata.json.",
            flush=True,
        )
    try:
        for stage in metadata["stages"]:
            stage.update(status="running", started_at=timestamp())
            save()
            print(f"\nRunning {stage['name']}...\nLog: {stage['log']}", flush=True)
            try:
                with Path(stage["log"]).open("wb") as log:
                    result = subprocess.run(
                        stage["command"], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT
                    )
            except KeyboardInterrupt:
                stage.update(status="interrupted", finished_at=timestamp())
                raise
            except OSError as error:
                stage.update(status="failed", finished_at=timestamp(), error=str(error))
                raise
            stage.update(
                status="completed" if result.returncode == 0 else "failed",
                returncode=result.returncode,
                finished_at=timestamp(),
            )
            save()
            if result.returncode:
                raise RuntimeError(
                    f"{stage['name']} failed with exit code {result.returncode}; see {stage['log']}"
                )
            if stage["name"] == "construction":
                try:
                    construction_metadata = json.loads(
                        (Path(stage["output_directory"]) / "metadata.json").read_text()
                    )
                    if construction_metadata.get("case_count", 0) <= 0:
                        raise ValueError("no construction cases were recorded")
                    stage["case_count"] = construction_metadata["case_count"]
                except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                    stage.update(status="failed", error=str(error))
                    raise RuntimeError(
                        f"Cannot validate construction results: {error}"
                    ) from error
                save()
                print("construction completed.", flush=True)
                continue

            validation = metadata["checksum_validation"]
            try:
                report = read_stage_results(
                    stage["name"],
                    Path(stage["output_directory"]),
                    args.runs,
                    args.full_pipeline,
                )
                validation["stages"][stage["name"]] = report
                if validation["baseline"] is None:
                    validation["baseline"] = report["checksums"][0]
                    validation["workload"] = report["workload"]
                if report["workload"] != validation["workload"]:
                    raise RuntimeError(
                        f"{stage['name']} scene/camera settings differ from the benchmark: "
                        f"expected {validation['workload']}; actual {report['workload']}"
                    )
                for trial, checksum in enumerate(report["checksums"], 1):
                    if checksum != validation["baseline"]:
                        raise RuntimeError(
                            f"Exact checksum mismatch in {stage['name']} trial {trial}: "
                            f"expected {validation['baseline']}; actual {checksum}. "
                            "No tolerance is allowed."
                        )
                report["status"] = "passed"
            except RuntimeError as error:
                stage.update(status="failed", error=str(error))
                validation.update(status="failed", error=str(error))
                validation["stages"].setdefault(stage["name"], {}).update(
                    status="failed", error=str(error)
                )
                raise
            if stage["name"] == "benchmark":
                try:
                    metadata["assembly_dump"].update(
                        capture_benchmark_binary(Path(stage["output_directory"]))
                    )
                except RuntimeError as error:
                    metadata["assembly_dump"].update(status="failed", error=str(error))
                    raise
            save()
            print(f"{stage['name']} completed; checksums match exactly.", flush=True)
        metadata["checksum_validation"]["status"] = "passed"
        metadata["assembly_dump"]["status"] = "running"
        save()
        print("\nWriting benchmark assembly dump...", flush=True)
        write_assembly_dump(metadata["assembly_dump"])
        metadata["summary"] = collect_summary(
            results,
            metadata["checksum_validation"],
            args.runs,
            args.full_pipeline,
            args.focus,
        )
        summary_text = format_summary(metadata["summary"])
        (results / "summary.json").write_text(
            json.dumps(metadata["summary"], indent=2, allow_nan=False) + "\n",
            encoding="utf-8",
        )
        (results / "summary.txt").write_text(summary_text, encoding="utf-8")
        metadata["status"] = "completed"
    except KeyboardInterrupt:
        metadata["status"] = "interrupted"
        raise
    except (OSError, RuntimeError):
        metadata["status"] = "failed"
        raise
    finally:
        if metadata["checksum_validation"]["status"] == "pending":
            metadata["checksum_validation"]["status"] = "incomplete"
        if metadata["assembly_dump"]["status"] == "pending":
            metadata["assembly_dump"]["status"] = "skipped"
        metadata["finished_at"] = timestamp()
        save()
    print(f"\nRenderer workflows completed with exactly matching checksums: {results}")
    if args.focus == "full":
        print("BVH construction analysis also completed.")
    print(f"Assembly dump: {metadata['assembly_dump']['path']}")
    view_command = [
        sys.executable,
        str(ROOT / "profile.py"),
        "--serve-only",
        str(results / "profile"),
    ]
    print(f"View graphs later: {shlex.join(view_command)}")
    print("\n" + summary_text, end="")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(f"Analysis failed: {error}")
    except KeyboardInterrupt:
        print(
            "\nAnalysis interrupted; partial results and logs were preserved.",
            file=sys.stderr,
        )
        raise SystemExit(130)
