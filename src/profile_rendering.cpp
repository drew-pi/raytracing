#include "scenes/benchmark_scene.h"

#include <scene.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

struct Options {
  int runs = 5;
  int threads = benchmark_thread_count;
  bool help = false;
  bool metadata = false;
  std::string perf_control;
  std::string perf_ack;
};

int positive_integer(std::string_view value, std::string_view option) {
  int result = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size() ||
      result <= 0) {
    throw std::invalid_argument(std::string(option) +
                                " must be a positive integer in int range");
  }
  return result;
}

Options parse_options(int argc, char *argv[]) {
  Options options;
  bool runs_seen = false;
  bool threads_seen = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--help") {
      options.help = true;
      continue;
    }
    if (option == "--metadata") {
      options.metadata = true;
      continue;
    }
    if (option != "--runs" && option != "--threads" &&
        option != "--perf-control" && option != "--perf-ack") {
      throw std::invalid_argument("Unknown argument: " + std::string(option));
    }
    if (i + 1 == argc || std::string_view(argv[i + 1]).starts_with("--") ||
        std::string_view(argv[i + 1]).empty()) {
      throw std::invalid_argument("Missing value for " + std::string(option));
    }
    const std::string_view value{argv[++i]};
    if (option == "--runs") {
      if (runs_seen) {
        throw std::invalid_argument("Duplicate --runs");
      }
      runs_seen = true;
      options.runs = positive_integer(value, option);
    } else if (option == "--threads") {
      if (threads_seen) {
        throw std::invalid_argument("Duplicate --threads");
      }
      threads_seen = true;
      options.threads = positive_integer(value, option);
    } else {
      auto &path =
          option == "--perf-control" ? options.perf_control : options.perf_ack;
      if (!path.empty()) {
        throw std::invalid_argument("Duplicate " + std::string(option));
      }
      path = value;
    }
  }
  if (options.metadata && (runs_seen || threads_seen)) {
    throw std::invalid_argument(
        "--metadata cannot be combined with --runs or --threads");
  }
  return options;
}

#if defined(__unix__) || defined(__APPLE__)

class FileDescriptor {
public:
  explicit FileDescriptor(int fd = -1) : fd_(fd) {}
  ~FileDescriptor() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  int get() const { return fd_; }

private:
  int fd_;
};

class PerfControl {
  using Clock = std::chrono::steady_clock;
  using Deadline = Clock::time_point;

  [[noreturn]] static void system_failure(const std::string &operation) {
    throw std::system_error(errno, std::generic_category(), operation);
  }

  static int remaining_ms(Deadline deadline) {
    const auto remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero()) {
      throw std::runtime_error(
          "Timed out waiting for perf FIFO handshake (30s)");
    }
    return static_cast<int>(
        std::chrono::ceil<std::chrono::milliseconds>(remaining).count());
  }

  static void pause(Deadline deadline) {
    const int remaining = remaining_ms(deadline);
    if (::poll(nullptr, 0, remaining < 10 ? remaining : 10) < 0 &&
        errno != EINTR) {
      system_failure("poll perf FIFO retry");
    }
  }

  static void require_fifo(const std::string &path) {
    struct stat status{};
    if (::lstat(path.c_str(), &status) < 0) {
      system_failure("stat perf FIFO " + path);
    }
    if (!S_ISFIFO(status.st_mode)) {
      throw std::runtime_error("Perf control path is not a FIFO: " + path);
    }
  }

  static int open_fifo(const std::string &path, int access, Deadline deadline) {
    require_fifo(path);
    for (;;) {
      remaining_ms(deadline);
      int flags = access | O_NONBLOCK | O_CLOEXEC;
#ifdef O_NOFOLLOW
      flags |= O_NOFOLLOW;
#endif
      const int fd = ::open(path.c_str(), flags);
      if (fd >= 0) {
        struct stat status{};
        if (::fstat(fd, &status) < 0) {
          const int error = errno;
          ::close(fd);
          throw std::system_error(error, std::generic_category(),
                                  "fstat perf FIFO " + path);
        }
        if (!S_ISFIFO(status.st_mode)) {
          ::close(fd);
          throw std::runtime_error("Perf control path is not a FIFO: " + path);
        }
        return fd;
      }
      if (errno == EINTR) {
        continue;
      }
      if (access == O_WRONLY && errno == ENXIO) {
        pause(deadline);
        continue;
      }
      system_failure("open perf FIFO " + path);
    }
  }

  static short wait(int fd, short events, Deadline deadline) {
    pollfd descriptor{fd, events, 0};
    for (;;) {
      const int result = ::poll(&descriptor, 1, remaining_ms(deadline));
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        system_failure("poll perf FIFO");
      }
      if (result == 0) {
        continue;
      }
      if (descriptor.revents & (POLLERR | POLLNVAL)) {
        throw std::runtime_error("Perf FIFO poll error");
      }
      return descriptor.revents;
    }
  }

public:
  PerfControl(const std::string &control, const std::string &ack)
      : PerfControl(control, ack, Clock::now() + std::chrono::seconds(30)) {}

  void command(std::string_view command) {
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (!command.empty()) {
      remaining_ms(deadline);
      const auto count =
          ::write(control_.get(), command.data(), command.size());
      if (count > 0) {
        command.remove_prefix(static_cast<std::size_t>(count));
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (wait(control_.get(), POLLOUT, deadline) & POLLHUP) {
          throw std::runtime_error("Perf control FIFO closed");
        }
      } else if (count < 0) {
        system_failure("write perf command");
      } else {
        throw std::runtime_error("Zero-length write to perf control FIFO");
      }
    }

    constexpr std::string_view expected = "ack\n";
    std::size_t received = 0;
    while (received < expected.size()) {
      remaining_ms(deadline);
      char byte;
      const auto count = ::read(ack_.get(), &byte, 1);
      if (count > 0) {
        // Some perf versions write sizeof("ack\\n"), including a trailing NUL.
        // Leave it for the next command and ignore it only between messages.
        if (received == 0 && byte == '\0') {
          continue;
        }
        if (byte != expected[received++]) {
          throw std::runtime_error("Expected ack\\n from perf control");
        }
      } else if (count == 0) {
        // A FIFO without a writer reports EOF/HUP immediately; do not spin.
        pause(deadline);
      } else if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (wait(ack_.get(), POLLIN, deadline) & POLLHUP) {
          pause(deadline);
        }
      } else {
        system_failure("read perf acknowledgement");
      }
    }
  }

private:
  PerfControl(const std::string &control, const std::string &ack,
              Deadline deadline)
      : ack_(open_fifo(ack, O_RDONLY, deadline)),
        control_(open_fifo(control, O_WRONLY, deadline)) {
    command("disable\n");
  }

  FileDescriptor ack_;
  FileDescriptor control_;
};

#else

class PerfControl {
public:
  PerfControl(const std::string &, const std::string &) {
    throw std::runtime_error("Perf controls are unsupported on this platform");
  }
  void command(std::string_view) {}
};

#endif

void require_finite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw std::runtime_error("Non-finite " + std::string(name));
  }
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    const auto options = parse_options(argc, argv);
    if (options.help) {
      std::cout
          << "Usage: " << argv[0]
          << " [--runs N] [--threads N] --perf-control PATH --perf-ack PATH\n"
             "       "
          << argv[0]
          << " --metadata | --help\n"
             "  --runs N       Positive trial count (default: 5)\n"
             "  --threads N    Positive worker count (default: "
          << benchmark_thread_count
          << ")\n"
             "  --metadata     Print scene JSON without loading assets "
             "or rendering\n"
             "  --perf-control PATH  perf command FIFO (required for "
             "rendering)\n"
             "  --perf-ack PATH      perf acknowledgement FIFO (required for "
             "rendering)\n"
             "Run rendering via profile.py with perf record --delay=-1.\n";
      return 0;
    }

    if (options.metadata) {
      write_benchmark_metadata(std::cout);
      return 0;
    }

    if (options.perf_control.empty() || options.perf_ack.empty()) {
      throw std::invalid_argument(
          "Rendering requires paired --perf-control PATH and --perf-ack PATH; "
          "run via profile.py");
    }
#if defined(__unix__) || defined(__APPLE__)
    // A disconnected perf reader must become a checked write error, not a
    // signal.
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
      throw std::system_error(errno, std::generic_category(), "ignore SIGPIPE");
    }
#endif
    PerfControl perf(options.perf_control, options.perf_ack);

    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](auto begin, auto end) {
      return std::chrono::duration<double, std::milli>(end - begin).count();
    };

    const auto scene_begin = Clock::now();
    auto [camera, meshes] = create_benchmark_scene();
    const auto scene_end = Clock::now();
    Scene<double> scene;
    scene.build(std::move(meshes));
    const auto build_end = Clock::now();

    const double scene_ms = milliseconds(scene_begin, scene_end);
    const double build_ms = milliseconds(scene_end, build_end);
    require_finite(scene_ms, "scene_ms");
    require_finite(build_ms, "build_ms");
    std::cout << std::setprecision(17)
              << "{\"type\":\"startup\",\"scene_ms\":" << scene_ms
              << ",\"build_ms\":" << build_ms << "}\n"
              << std::flush;
    if (!std::cout) {
      throw std::runtime_error("Cannot write startup JSON");
    }

    for (int trial = 0; trial < options.runs; ++trial) {
      perf.command("enable\n");
      const auto render_begin = Clock::now();
      const auto checksum =
          camera.render_checksum(scene, options.threads, false);
      const auto render_end = Clock::now();
      perf.command("disable\n");

      const double render_ms = milliseconds(render_begin, render_end);
      require_finite(render_ms, "render_ms");
      require_finite(checksum.x(), "checksum red");
      require_finite(checksum.y(), "checksum green");
      require_finite(checksum.z(), "checksum blue");
      std::cout << "{\"type\":\"render\",\"run\":" << trial + 1
                << ",\"render_ms\":" << render_ms << ",\"checksum\":["
                << checksum.x() << ',' << checksum.y() << ',' << checksum.z()
                << "]}\n"
                << std::flush;
      if (!std::cout) {
        throw std::runtime_error("Cannot write render JSON");
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Rendering profiler: " << error.what() << '\n';
    return 1;
  }
}
