#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace pandora {

// Bounded FIFO worker pool for offloading DB / slow IO off hot paths.
class AsyncWorker {
 public:
  explicit AsyncWorker(int threads = 2, std::size_t max_queue = 100000);
  ~AsyncWorker();

  AsyncWorker(const AsyncWorker&) = delete;
  AsyncWorker& operator=(const AsyncWorker&) = delete;

  void Start();
  void Stop();
  bool Post(std::function<void()> job);
  std::size_t Pending() const;

 private:
  void Loop();

  int threads_{2};
  std::size_t max_queue_{100000};
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> q_;
  std::vector<std::thread> workers_;
  bool stopping_{false};
  bool started_{false};
};

}  // namespace pandora
