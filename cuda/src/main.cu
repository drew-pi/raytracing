#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cuda_runtime.h>
#include <iostream>
#include <vector>

#define CUDA_CHECK(call)                                                       \
  do {                                                                         \
    const cudaError_t error = (call);                                          \
    if (error != cudaSuccess) {                                                \
      std::cerr << "CUDA error: " << cudaGetErrorString(error) << " ("         \
                << __FILE__ << ':' << __LINE__ << ")\n";                       \
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
  const std::size_t i =
      blockIdx.x * static_cast<std::size_t>(blockDim.x) + threadIdx.x;

  if (i < n) {
    y[i] += x[i];
  }
}

int main() {
  constexpr std::size_t N = 1 << 30;
  const std::size_t bytes = N * sizeof(float);

  float *x = nullptr;
  float *gpu_y = nullptr;

  // Allocate memory accessible from the CPU and GPU.
  CUDA_CHECK(cudaMallocManaged(&x, bytes));
  CUDA_CHECK(cudaMallocManaged(&gpu_y, bytes));

  std::vector<float> cpu_y(N);

  // Initialize data on the CPU.
  for (std::size_t i = 0; i < N; ++i) {
    x[i] = static_cast<float>(i % 1000) / 1000.0f;

    const float initial_y = static_cast<float>(i % 500) / 500.0f;

    cpu_y[i] = initial_y;
    gpu_y[i] = initial_y;
  }

  // Run and time the CPU implementation.
  const auto cpu_start = std::chrono::steady_clock::now();

  cpu_add(N, x, cpu_y.data());

  const auto cpu_stop = std::chrono::steady_clock::now();

  const double cpu_ms =
      std::chrono::duration<double, std::milli>(cpu_stop - cpu_start).count();

  // Launch the GPU implementation.
  constexpr int threads = 256;
  const int blocks = static_cast<int>((N + threads - 1) / threads);

  CUDA_CHECK(cudaMemPrefetchAsync(x, bytes, 0, 0));
  CUDA_CHECK(cudaMemPrefetchAsync(gpu_y, bytes, 0, 0));

  gpu_add<<<blocks, threads>>>(N, x, gpu_y);

  CUDA_CHECK(cudaMemPrefetchAsync(gpu_y, bytes, cudaCpuDeviceId, 0));

  // Check launch and execution errors.
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaDeviceSynchronize());

  // Validate the result.
  float max_error = 0.0f;

  for (std::size_t i = 0; i < N; ++i) {
    const float difference = std::fabs(cpu_y[i] - gpu_y[i]);

    max_error = std::max(max_error, difference);
  }

  std::cout << "Elements:  " << N << '\n';
  std::cout << "CPU time:  " << cpu_ms << " ms\n";
  std::cout << "Max error: " << max_error << '\n';

  CUDA_CHECK(cudaFree(x));
  CUDA_CHECK(cudaFree(gpu_y));

  return max_error <= 1e-6f ? EXIT_SUCCESS : EXIT_FAILURE;
}
