#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#if !defined(__linux__)
static_assert(false, "backend-common perf utilities only support Linux");
#endif

#include <sys/resource.h>
#include <unistd.h>

namespace bonc::backend_common {

class Timer {
 public:
  using clock = std::chrono::steady_clock;

  Timer() : start_(clock::now()) {}

  void reset() { start_ = clock::now(); }

  [[nodiscard]] std::chrono::nanoseconds elapsed() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start_);
  }

  template <class Duration>
  [[nodiscard]] Duration elapsed_as() const {
    return std::chrono::duration_cast<Duration>(clock::now() - start_);
  }

 private:
  clock::time_point start_;
};

template <class Fn>
class ScopedTimer {
 public:
  explicit ScopedTimer(Fn on_finish) : on_finish_(std::move(on_finish)) {}

  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;

  ScopedTimer(ScopedTimer&& other) noexcept
      : timer_(other.timer_), on_finish_(std::move(other.on_finish_)), active_(other.active_) {
    other.active_ = false;
  }

  ScopedTimer& operator=(ScopedTimer&&) = delete;

  ~ScopedTimer() {
    if (!active_) {
      return;
    }
    on_finish_(timer_.elapsed());
  }

  void cancel() { active_ = false; }

 private:
  Timer timer_{};
  Fn on_finish_;
  bool active_ = true;
};

enum class MemoryUnit : uint8_t {
  Bytes,
  KiB,
};

[[nodiscard]] inline std::optional<std::uint64_t> parse_proc_status_kib(std::string_view key) {
  // Parses /proc/self/status lines like:
  // VmRSS:     12345 kB
  // VmHWM:     23456 kB
  std::ifstream in("/proc/self/status");
  if (!in.good()) {
    return std::nullopt;
  }

  std::string line;
  while (std::getline(in, line)) {
    if (!line.starts_with(key)) {
      continue;
    }

    std::istringstream iss(line);
    std::string label;
    std::uint64_t value = 0;
    std::string unit;
    iss >> label >> value >> unit;
    if (!iss.fail() && (unit == "kB" || unit == "KB")) {
      return value;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] inline std::optional<std::uint64_t> current_rss_bytes() {
  // /proc/self/statm: size resident shared text lib data dt
  // resident is in pages.
  std::ifstream in("/proc/self/statm");
  if (!in.good()) {
    return std::nullopt;
  }

  std::uint64_t size_pages = 0;
  std::uint64_t resident_pages = 0;
  in >> size_pages >> resident_pages;
  if (in.fail()) {
    return std::nullopt;
  }

  const long page_size = ::sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return std::nullopt;
  }

  return resident_pages * static_cast<std::uint64_t>(page_size);
}

[[nodiscard]] inline std::optional<std::uint64_t> peak_rss_bytes() {
  // Prefer VmHWM (High Water Mark of RSS) if available.
  if (auto vmhwm_kib = parse_proc_status_kib("VmHWM:")) {
    return (*vmhwm_kib) * 1024ULL;
  }

  // Fallback to getrusage. On Linux, ru_maxrss is in KiB.
  rusage usage{};
  if (::getrusage(RUSAGE_SELF, &usage) != 0) {
    return std::nullopt;
  }

  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
}

struct ResourceSample {
  std::optional<std::uint64_t> rss_bytes;
  std::optional<std::uint64_t> peak_rss_bytes;
};

[[nodiscard]] inline ResourceSample sample_resources() {
  return ResourceSample{.rss_bytes = current_rss_bytes(), .peak_rss_bytes = peak_rss_bytes()};
}

}  // namespace bonc::backend_common
