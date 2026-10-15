#!/usr/bin/env python3

import argparse
import getpass
import shlex
import shutil
import socket
import subprocess
import time
from datetime import datetime
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build-profile"
FLAMEGRAPH = Path.home() / "prometheus" / "dev" / "tools" / "FlameGraph"


def build():
    subprocess.run(
        [
            "cmake",
            "-S",
            str(ROOT),
            "-B",
            str(BUILD),
            "-DCMAKE_CXX_COMPILER=g++",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O3 -g -DNDEBUG -march=native",
        ],
        cwd=ROOT,
        check=True,
    )
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
        cwd=ROOT,
        check=True,
    )


def find_executable():
    for candidate in (
        BUILD / "RelWithDebInfo" / "raytracer_bench",
        BUILD / "raytracer_bench",
    ):
        if candidate.is_file():
            return candidate

    raise RuntimeError("Cannot find raytracer_bench; build it first.")


def run_with_progress(command, destination, description):
    print(f"{description}...", flush=True)
    started = time.monotonic()
    with destination.open("wb") as output:
        process = subprocess.Popen(command, cwd=ROOT, stdout=output)
        try:
            while True:
                try:
                    returncode = process.wait(timeout=10)
                    break
                except subprocess.TimeoutExpired:
                    elapsed = time.monotonic() - started
                    size = destination.stat().st_size / (1024 * 1024)
                    print(
                        f"  {description}: still running, {elapsed:.0f}s elapsed, "
                        f"{destination.name}: {size:.1f} MiB written "
                        "(buffering may delay file growth)",
                        flush=True,
                    )
            if returncode:
                raise subprocess.CalledProcessError(returncode, command)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    elapsed = time.monotonic() - started
    print(f"  {description}: complete in {elapsed:.1f}s", flush=True)


def skip_consecutive_frames(path):
    collapsed = {}
    for line in path.read_text().splitlines():
        stack, count = line.rsplit(" ", 1)
        frames = []
        for frame in stack.split(";"):
            if not frames or frames[-1] != frame:
                frames.append(frame)
        key = ";".join(frames)
        collapsed[key] = collapsed.get(key, 0) + int(count)

    with path.open("w") as output:
        for stack, count in sorted(collapsed.items()):
            output.write(f"{stack} {count}\n")


def generate_diff(baseline_dir, profile_dir, event, folded_name, label):
    diff_name = "cycles-diff" if event == "cycles:u" else "cache-misses-diff"
    diff_folded = profile_dir / f"{diff_name}.folded"
    diff_svg = profile_dir / f"{diff_name}.svg"
    before_total = sum(
        int(line.rsplit(" ", 1)[1])
        for line in (baseline_dir / folded_name).read_text().splitlines()
    )
    after_total = sum(
        int(line.rsplit(" ", 1)[1])
        for line in (profile_dir / folded_name).read_text().splitlines()
    )
    change = (
        f"{(after_total / before_total - 1) * 100:+.1f}%"
        if before_total
        else "percentage unavailable"
    )
    # FlameGraph uses its second weight column for width. Reverse the inputs
    # to retain baseline-only paths, then negate colors to keep decreases blue.
    run_with_progress(
        [
            "perl",
            str(FLAMEGRAPH / "difffolded.pl"),
            str(profile_dir / folded_name),
            str(baseline_dir / folded_name),
        ],
        diff_folded,
        f"Comparing {label} against baseline",
    )
    run_with_progress(
        [
            "perl",
            str(FLAMEGRAPH / "flamegraph.pl"),
            "--negate",
            "--title",
            f"{label.capitalize()}: after vs before (baseline widths, unnormalized)",
            "--subtitle",
            f"Sampled weight: {before_total:,} -> {after_total:,} ({change}); "
            "width=before; blue=decrease, red=increase",
            "--countname",
            label,
            str(diff_folded),
        ],
        diff_svg,
        f"Generating {diff_svg.name}",
    )
    print(f"Diff flamegraph saved: {diff_svg}", flush=True)
    return diff_svg


def serve_flamegraph(svgs, port, ssh_host):
    handler = partial(
        SimpleHTTPRequestHandler,
        directory=str(svgs[0].parent),
    )

    with ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        actual_port = server.server_address[1]
        destination = shlex.quote(ssh_host)

        urls = "\n".join(f"  http://localhost:{actual_port}/{svg.name}" for svg in svgs)
        print(
            "\nFlamegraph server is running on this machine's loopback interface.\n"
            "\nRun this command in a terminal on your LOCAL machine:\n"
            f"  ssh -N -o ExitOnForwardFailure=yes "
            f"-L {actual_port}:127.0.0.1:{actual_port} {destination}\n"
            "\nThen open these URLs in your LOCAL browser:\n"
            f"{urls}\n"
            "\nKeep this script and the SSH tunnel running while viewing.\n"
            "Press Ctrl+C here to stop the server.",
            flush=True,
        )

        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nFlamegraph server stopped.")


def main():
    parser = argparse.ArgumentParser(
        description="Build, profile, and serve a ray tracer flamegraph."
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="reuse the existing benchmark executable",
    )
    parser.add_argument(
        "--skip-consecutive",
        action="store_true",
        help="merge adjacent identical frames to hide direct recursion depth",
    )
    parser.add_argument(
        "--no-cache-misses",
        action="store_true",
        help="record cycles only (cache-miss sampling is enabled by default)",
    )
    comparison = parser.add_mutually_exclusive_group()
    comparison.add_argument(
        "--compare-only",
        type=Path,
        nargs=2,
        metavar=("BASELINE", "PROFILE"),
        help="compare two saved profiles without building or recording; first is baseline",
    )
    comparison.add_argument(
        "--compare",
        type=Path,
        metavar="PROFILE_DIR",
        help="compare this run against a saved profile (raw sampled event weights)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8000,
        help="HTTP server and local tunnel port (default: 8000; 0 selects a free port)",
    )
    parser.add_argument(
        "--ssh-host",
        default=f"{getpass.getuser()}@{socket.gethostname().split('.')[0]}",
        help="SSH destination (default: current user@short hostname)",
    )
    args = parser.parse_args()

    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")

    if args.compare_only and args.skip_consecutive:
        parser.error(
            "--compare-only uses saved folded stacks; use profiles generated "
            "with matching --skip-consecutive settings instead"
        )

    commands = ["perl"]
    if args.compare_only is None:
        commands.append("perf")
        if not args.skip_build:
            commands.append("cmake")

    for command in commands:
        if shutil.which(command) is None:
            raise RuntimeError(f"Required command not found: {command}")

    required_scripts = [FLAMEGRAPH / "flamegraph.pl"]
    if args.compare_only is None:
        required_scripts.append(FLAMEGRAPH / "stackcollapse-perf.pl")
    if args.compare is not None or args.compare_only is not None:
        required_scripts.append(FLAMEGRAPH / "difffolded.pl")
    for path in required_scripts:
        if not path.is_file():
            raise RuntimeError(
                f"Required file not found: {path}\n"
                "From the project root, install FlameGraph with:\n"
                "  git clone https://github.com/brendangregg/FlameGraph.git "
                "../FlameGraph"
            )

    events = [("cycles:u", "stacks.folded", "flamegraph.svg", "cycles")]
    if not args.no_cache_misses:
        events.append(
            (
                "cache-misses:u",
                "cache-misses.folded",
                "cache-misses.svg",
                "cache misses",
            )
        )

    if args.compare_only is not None:
        baseline_dir, profile_dir = (
            path.expanduser().resolve() for path in args.compare_only
        )
        if baseline_dir == profile_dir:
            parser.error("--compare-only requires two different profile directories")
        for directory in (baseline_dir, profile_dir):
            for _, folded_name, _, _ in events:
                folded = directory / folded_name
                if not folded.is_file() or not folded.stat().st_size:
                    raise RuntimeError(
                        f"Comparison requires a nonempty saved file: {folded}. "
                        "For cycles-only profiles, use --no-cache-misses."
                    )
        print(
            f"Comparing {baseline_dir} (before) against {profile_dir} (after).\n"
            "No build or recording will run. Existing diff files in the after "
            "directory will be replaced.\n"
            "Diffs are unnormalized: red=increased event weight, blue=decreased event weight. "
            "Use matching workloads and saved recursion settings.",
            flush=True,
        )
        svgs = [
            generate_diff(baseline_dir, profile_dir, event, folded_name, label)
            for event, folded_name, _, label in events
        ]
        serve_flamegraph(svgs, args.port, args.ssh_host)
        return

    if args.compare is not None:
        args.compare = args.compare.expanduser().resolve()
        for _, folded_name, _, _ in events:
            baseline = args.compare / folded_name
            if not baseline.is_file() or not baseline.stat().st_size:
                raise RuntimeError(
                    f"Comparison requires a nonempty baseline file: {baseline}. "
                    "If the baseline has cycles only, use --no-cache-misses."
                )
        print(
            f"Comparing against {args.compare} (before); the new run is after.\n"
            "Diffs use raw sampled event weights: red means increased weight, "
            "blue means decreased weight, not direct runtime changes.\n"
            "Use the same workload and --skip-consecutive setting as the baseline.",
            flush=True,
        )

    if not args.skip_build:
        build()

    benchmark = find_executable()

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    profile_dir = ROOT / "benchmark-results" / f"profile-{timestamp}"
    profile_dir.parent.mkdir(parents=True, exist_ok=True)
    profile_dir.mkdir()

    data = profile_dir / "perf.data"
    stacks = profile_dir / "perf.script"

    print(f"Recording into {profile_dir}", flush=True)
    subprocess.run(
        [
            "perf",
            "record",
            "-o",
            str(data),
            "-e",
            ",".join(event for event, _, _, _ in events),
            "-F",
            "99",
            "--call-graph",
            "dwarf,16384",
            "--",
            str(benchmark),
        ],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        check=True,
    )

    print(
        "\nRecording complete. Exporting and symbolizing DWARF stacks can take "
        "a while—tens of seconds to several minutes, or longer for large/deep "
        "recordings. The benchmark will not run again.\n"
        "Status updates appear every 10 seconds; elapsed time and output size "
        "are activity indicators, not a percentage or time-remaining estimate.",
        flush=True,
    )
    run_with_progress(
        ["perf", "script", "-i", str(data)],
        stacks,
        "Exporting call stacks with perf script",
    )

    svgs = []
    for event, folded_name, svg_name, label in events:
        folded = profile_dir / folded_name
        svg = profile_dir / svg_name
        run_with_progress(
            [
                "perl",
                str(FLAMEGRAPH / "stackcollapse-perf.pl"),
                f"--event-filter={event}",
                str(stacks),
            ],
            folded,
            f"Collapsing {label} stacks",
        )
        if not folded.stat().st_size:
            raise RuntimeError(
                f"No stacks found for {event}; inspect {stacks}. "
                "For cycles-only recording, use --no-cache-misses."
            )
        if args.skip_consecutive:
            skip_consecutive_frames(folded)

        run_with_progress(
            [
                "perl",
                str(FLAMEGRAPH / "flamegraph.pl"),
                "--title",
                f"Ray tracer — userspace {label}",
                "--countname",
                label,
                str(folded),
            ],
            svg,
            f"Generating {svg.name}",
        )
        print(f"Flamegraph saved: {svg}", flush=True)
        svgs.append(svg)

        if args.compare is not None:
            svgs.append(
                generate_diff(args.compare, profile_dir, event, folded_name, label)
            )

    serve_flamegraph(svgs, args.port, args.ssh_host)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Profiling failed: {error}")
    except KeyboardInterrupt:
        raise SystemExit("\nProfiling interrupted.")
