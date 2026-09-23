#include "net/iocp_pool.hpp"

#include "common/log.hpp"

namespace pandora {

namespace {
constexpr ULONG_PTR kShutdownKey = static_cast<ULONG_PTR>(-1);
}

IocpPool::IocpPool() = default;

IocpPool::~IocpPool() { Stop(); }

bool IocpPool::Start(int worker_count, Handler handler) {
  if (iocp_) return false;
  if (worker_count < 1) worker_count = 1;
  if (worker_count > 512) worker_count = 512;
  handler_ = std::move(handler);
  iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, static_cast<DWORD>(worker_count));
  if (!iocp_) {
    PLOG_ERROR("CreateIoCompletionPort failed err=" << GetLastError());
    return false;
  }
  stopping_ = false;
  worker_count_ = worker_count;
  workers_.reserve(static_cast<size_t>(worker_count));
  for (int i = 0; i < worker_count; ++i) {
    workers_.emplace_back([this]() { WorkerLoop(); });
  }
  PLOG_INFO("IOCP pool started workers=" << worker_count_);
  return true;
}

void IocpPool::Stop() {
  if (!iocp_) return;
  stopping_ = true;
  for (size_t i = 0; i < workers_.size(); ++i) {
    PostQueuedCompletionStatus(static_cast<HANDLE>(iocp_), 0, kShutdownKey, nullptr);
  }
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  workers_.clear();
  CloseHandle(static_cast<HANDLE>(iocp_));
  iocp_ = nullptr;
  worker_count_ = 0;
}

bool IocpPool::Associate(SOCKET sock, ULONG_PTR key) {
  if (!iocp_) return false;
  return CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), static_cast<HANDLE>(iocp_), key, 0) != nullptr;
}

bool IocpPool::Post(ULONG_PTR key) {
  if (!iocp_ || stopping_) return false;
  return PostQueuedCompletionStatus(static_cast<HANDLE>(iocp_), 0, key, nullptr) != 0;
}

void IocpPool::WorkerLoop() {
  while (!stopping_) {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* ov = nullptr;
    const BOOL ok = GetQueuedCompletionStatus(static_cast<HANDLE>(iocp_), &bytes, &key, &ov, INFINITE);
    if (key == kShutdownKey) break;
    if (handler_) {
      try {
        handler_(key, bytes, ov, ok == TRUE);
      } catch (...) {
        PLOG_WARN("IOCP handler exception");
      }
    }
  }
}

}  // namespace pandora
