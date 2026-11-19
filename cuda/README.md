# Launch Codes

Run these commands from the repository root.

## Configure

My personal RTX 3080 Ti uses CUDA architecture `86`.

```sh
cmake -S cuda -B build/cuda \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_ARCHITECTURES=86
```

## Build

```sh
cmake --build build/cuda --parallel
```

## Run

```sh
./build/cuda/raytracer_cuda
```

After changing CUDA source files, only rebuild and run:

```sh
cmake --build build/cuda --parallel
./build/cuda/raytracer_cuda
```

# CUDA Ray Tracer

## Directory Structure

```text
cuda/
├── CMakeLists.txt
├── README.md
├── include/       # CUDA headers and shared device structures
└── src/
    └── main.cu    # CUDA program entry point
```
