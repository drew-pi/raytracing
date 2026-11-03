# Launch Codes

Run these commands from the repository root so relative asset paths resolve correctly. All generated build files live under `build/`.

CMake configures the project and generates build files for your compiler. The `cmake -S . -B ...` command selects the source directory, build directory, compiler, and build options. The `cmake --build ... --parallel` command compiles and links the executables using parallel jobs.

## Release

Optimized for rendering performance, with link-time optimization (LTO) enabled.

```sh
cmake -S . -B build/release \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native -fno-math-errno -fno-trapping-math"

cmake --build build/release --parallel
```

Run:

```sh
./build/release/raytracer > image.ppm
```

## Profiling

Optimized with debugging information retained and LTO enabled.

```sh
cmake -S . -B build/profile \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O3 -g -DNDEBUG -march=native -fno-math-errno -fno-trapping-math"

cmake --build build/profile --parallel
```

Run:

```sh
./build/profile/raytracer > image.ppm
```

## Debug

Disables optimizations and LTO, retains debugging information, and keeps assertions enabled.

```sh
cmake -S . -B build/debug \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
  -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g"

cmake --build build/debug --parallel
```

Run:

```sh
./build/debug/raytracer > image.ppm
```

## Notes

- `--parallel` allows the build tool to compile multiple files concurrently.
- `-march=native` targets the current CPU. The executable may not run on older or different CPUs.
- `-DNDEBUG` disables assertions in Release and Profiling builds.
- LTO enables optimization across translation units, but it can make profiling call stacks harder to interpret.
- For stack-sampling profilers, consider adding `-fno-omit-frame-pointer` to the profiling flags.
- If a build directory was previously configured with a different compiler, delete that specific directory before configuring it with `g++`.

### Python benchmark script

#### usage

Configure, build, and run 10 trials.

`python3 benchmark.py --runs 10`

Reuse the existing executable.

`python3 benchmark.py --runs 10 --skip-build`

Change the name of the save folder

`python3 benchmark.py --runs 10 --name saved-folder`

Use a certain number of threads

`python3 benchmark.py --runs 10 --name saved-folder --threads 4`

Do not save this run to disk

`python3 benchmark.py --runs 1 --threads 1 --no-save`

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
