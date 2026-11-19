#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
#include <vector>

#define CUDA_CHECK(call)                                                       \
  do {                                                                         \
    const cudaError_t error = call;                                            \
    if (error != cudaSuccess) {                                                \
      std::fprintf(stderr, "CUDA error: %s\n", cudaGetErrorString(error));     \
      std::exit(EXIT_FAILURE);                                                 \
    }                                                                          \
  } while (false)

// Runs on the CPU.
void cpu_add(std::size_t n, const float *x, float *y) {
  for (std::size_t i = 0; i < n; ++i) {
    y[i] += x[i];
  }
}

// Runs on the GPU.
__global__ void gpu_add(std::size_t n, const float *x, float *y) {
  const std::size_t start =
      blockIdx.x * static_cast<std::size_t>(blockDim.x) + threadIdx.x;

  const std::size_t stride = static_cast<std::size_t>(blockDim.x) * gridDim.x;

  for (std::size_t i = start; i < n; i += stride) {
    y[i] += x[i];
  }
}

int main() {
  constexpr std::size_t N = 1 << 30;

  std::vector<float> x(N);
  std::vector<float> initial_y(N);
  std::vector<float> cpu_y(N);
  std::vector<float> gpu_y(N);

  for (std::size_t i = 0; i < N; ++i) {
    x[i] = static_cast<float>(i % 1000) / 1000.0f;
    initial_y[i] = static_cast<float>(i % 500) / 500.0f;
  }

  // Initialize CUDA before timing.
  CUDA_CHECK(cudaFree(nullptr));

  // CPU benchmark.
  cpu_y = initial_y;

  const auto cpu_start = std::chrono::steady_clock::now();
  cpu_add(N, x.data(), cpu_y.data());
  const auto cpu_stop = std::chrono::steady_clock::now();

  const double cpu_ms =
      std::chrono::duration<double, std::milli>(cpu_stop - cpu_start).count();

  // Allocate GPU memory.
  float *device_x = nullptr;
  float *device_y = nullptr;

  CUDA_CHECK(cudaMalloc(&device_x, N * sizeof(float)));
  CUDA_CHECK(cudaMalloc(&device_y, N * sizeof(float)));

  CUDA_CHECK(cudaMemcpy(device_x, x.data(), N * sizeof(float),
                        cudaMemcpyHostToDevice));

  CUDA_CHECK(cudaMemcpy(device_y, initial_y.data(), N * sizeof(float),
                        cudaMemcpyHostToDevice));

  constexpr int threads = 256;
  const int blocks =
      std::min<int>(static_cast<int>((N + threads - 1) / threads), 4096);

  // GPU kernel-only benchmark.
  cudaEvent_t gpu_start;
  cudaEvent_t gpu_stop;

  CUDA_CHECK(cudaEventCreate(&gpu_start));
  CUDA_CHECK(cudaEventCreate(&gpu_stop));

  CUDA_CHECK(cudaEventRecord(gpu_start));

  gpu_add<<<blocks, threads>>>(N, device_x, device_y);
  CUDA_CHECK(cudaGetLastError());

  CUDA_CHECK(cudaEventRecord(gpu_stop));
  CUDA_CHECK(cudaEventSynchronize(gpu_stop));

  float gpu_ms = 0.0f;
  CUDA_CHECK(cudaEventElapsedTime(&gpu_ms, gpu_start, gpu_stop));

  CUDA_CHECK(cudaMemcpy(gpu_y.data(), device_y, N * sizeof(float),
                        cudaMemcpyDeviceToHost));

  // Check correctness.
  bool valid = true;

  for (std::size_t i = 0; i < N; ++i) {
    const float expected = x[i] + initial_y[i];

    if (std::abs(cpu_y[i] - expected) > 1e-6f ||
        std::abs(gpu_y[i] - expected) > 1e-6f) {
      valid = false;
      break;
    }
  }

  std::printf("Elements:       %zu\n", N);
  std::printf("CPU:     %.3f ms\n", cpu_ms);
  std::printf("GPU kernel:     %.3f ms\n", gpu_ms);
  std::printf("GPU speedup:    %.2fx\n", cpu_ms / gpu_ms);
  std::printf("Results valid:  %s\n", valid ? "yes" : "no");

  CUDA_CHECK(cudaEventDestroy(gpu_start));
  CUDA_CHECK(cudaEventDestroy(gpu_stop));
  CUDA_CHECK(cudaFree(device_x));
  CUDA_CHECK(cudaFree(device_y));

  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
