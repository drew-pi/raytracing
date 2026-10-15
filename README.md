# TODO

-

# Launch Codes

Before building or executing must keep the cmake configuration up to date

```bash
cmake -S . -B build \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

To build the project run the following command from the root of the project

`cmake --build build`

Then to run the executable use

`./build/raytracer > image.ppm`

## In order to compile for speed use

```bash
cmake -S . -B release \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native"
```

`cmake --build release --parallel`

`./release/raytracer > image.ppm`

## For benchmarking

```bash
cmake -S . -B build-profile \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -march=native"
```

`cmake --build build-profile -j --parallel`

`./build-profile/raytracer_bench > /dev/null`

### Python benchmark script

#### usage

Configure, build, and run 10 trials.

`python3 benchmark.py --runs 10`

Reuse the existing executable.

`python3 benchmark.py --runs 10 --skip-build`

### Profiling

Run to allow for profiling (somewhat dangerous so want to set it back to default afterwards)

`sudo sysctl -w kernel.perf_event_paranoid=2`

To set it back to default use

`sudo sysctl -w kernel.perf_event_paranoid=4`

and confirm it worked (expected number is 4)

`cat /proc/sys/kernel/perf_event_paranoid`

On the profiling machine (builds, records cycles/cache misses, and serves the graphs):

```bash
python3 profile.py
```

Options:

- `--skip-build`: reuse the executable instead of building.
- `--skip-consecutive`: merge adjacent identical frames to simplify recursion.
- `--no-cache-misses`: record cycles only (default: cycles and cache misses).
- `--compare PROFILE_DIR`: generate unnormalized diffs against a saved profile.
- `--compare-only BASELINE PROFILE`: compare two saved profiles without building or recording; first is baseline.
- `--ssh-host USER@HOST`: SSH destination for tunnel instructions (default: current user@short hostname).
- `--port PORT`: server/tunnel port (default: 8000; 0 selects a free server port).
- `-h`, `--help`: show usage. Processing can take several minutes.

On your local machine, start the tunnel:

```bash
ssh -N -L 8000:127.0.0.1:8000 username@hostname
```

Open http://localhost:8000/flamegraph.svg and http://localhost:8000/cache-misses.svg.
Keep both terminals running; Ctrl+C stops them. If changing the port, update both commands and URLs.

Compare a new run against a saved profile:

```bash
python3 profile.py --compare profile-BEFORE
```

Or compare two existing profiles (first is baseline):

```bash
python3 profile.py --compare-only profile-BEFORE profile-AFTER
```

Diffs are saved in the AFTER directory, replacing previous diff files, and served automatically. Both profiles need saved folded stacks; add `--no-cache-misses` for cycles-only comparison.
The output directory contains `cycles-diff.svg` and `cache-misses-diff.svg`, available at the same server URLs.
Diffs use baseline widths and raw sampled weights: blue = decreased self-weight, red = increased self-weight; totals and percentage change appear above the graph. Baseline-only paths stay visible; new-only paths have zero width. This is not a direct speedup measurement.
Use the same workload and `--skip-consecutive` setting for both profiles.
Results are saved in `benchmark-results/profile-*/`; cache-miss graphs are omitted when disabled.
