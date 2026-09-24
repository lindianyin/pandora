#include "common/async_worker.hpp"

#include "common/log.hpp"

namespace pandora {

AsyncWorker::AsyncWorker(int threads, std::size_t max_queue)
    : threads_(threads < 1 ? 1 : threads), max_queue_(max_queue < 1024 ? 1024 : max_queue) {}

AsyncWorker::~AsyncWorker() { Stop(); }

void AsyncWorker::Start() {
  std::lock_guard<std::mutex> lk(mu_);
  if (started_) return;
  stopping_ = false;
  started_ = true;
  workers_.reserve(static_cast<std::size_t>(threads_));
  for (int i = 0; i < threads_; ++i) {
    workers_.emplace_back([this] { Loop(); });
  }
  PLOG_INFO("async worker started threads=" << threads_ << " max_queue=" << max_queue_);
}

void AsyncWorker::Stop() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (!started_) return;
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  workers_.clear();
  {
    std::lock_guard<std::mutex> lk(mu_);
    started_ = false;
    // drop remaining jobs
    std::queue<std::function<void()>> empty;
    q_.swap(empty);
  }
}

bool AsyncWorker::Post(std::function<void()> job) {
  if (!job) return false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (stopping_ || !started_) return false;
    if (q_.size() >= max_queue_) {
      PLOG_WARN("async worker queue full, drop job size=" << q_.size());
      return false;
    }
    q_.push(std::move(job));
  }
  cv_.notify_one();
  return true;
}

std::size_t AsyncWorker::Pending() const {
  std::lock_guard<std::mutex> lk(mu_);
  return q_.size();
}

void AsyncWorker::Loop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [this] { return stopping_ || !q_.empty(); });
      if (stopping_ && q_.empty()) return;
      job = std::move(q_.front());
      q_.pop();
    }
    try {
      job();
    } catch (const std::exception& e) {
      PLOG_WARN("async worker job exception: " << e.what());
    } catch (...) {
      PLOG_WARN("async worker job exception");
    }
  }
}

}  // namespace pandora
