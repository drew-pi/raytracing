# Launch Codes

Run these commands from the repository root.

My personal RTX 3080 Ti uses CUDA architecture `86`.

## Release

The release profile builds an optimized version of the CUDA ray tracer.

### Configure

```sh
cmake -S cuda -B build/cuda-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_ARCHITECTURES=86
```

### Build

```sh
cmake --build build/cuda-release --parallel
```

### Run

```sh
./build/cuda-release/raytracer_cuda
```

After changing CUDA source files, only rebuild and run:

```sh
cmake --build build/cuda-release --parallel
./build/cuda-release/raytracer_cuda
```

## Profile

The profile build is optimized while retaining debugging information for profiling.

### Configure

```sh
cmake -S cuda -B build/cuda-profile \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CUDA_ARCHITECTURES=86
```

### Build

```sh
cmake --build build/cuda-profile --parallel
```

### Run

```sh
./build/cuda-profile/raytracer_cuda
```

After changing CUDA source files, only rebuild and run:

```sh
cmake --build build/cuda-profile --parallel
./build/cuda-profile/raytracer_cuda
```

### Run with NVIDIA Nsight Systems

Remove previous reports and profile the application:

```sh
rm -f build/cuda-profile/report.nsys-rep
rm -f build/cuda-profile/report.sqlite

nsys profile \
  --force-overwrite=true \
  --output=build/cuda-profile/report \
  ./build/cuda-profile/raytracer_cuda
```

### View the Terminal Summary

```sh
nsys stats build/cuda-profile/report.nsys-rep
```

### Generate the Markdown Report

```sh
./cuda/generate_report.sh
```

# CUDA Ray Tracer

## Directory Structure

```text
cuda/
├── CMakeLists.txt
├── README.md
├── generate_report.sh
├── include/       # CUDA headers and shared device structures
└── src/
    └── main.cu    # CUDA program entry point
```
