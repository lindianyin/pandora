#pragma once

#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#endif

namespace pandora {

class IocpPool {
 public:
  using Handler = std::function<void(ULONG_PTR key, DWORD bytes, OVERLAPPED* ov, bool ok)>;

  IocpPool();
  ~IocpPool();

  IocpPool(const IocpPool&) = delete;
  IocpPool& operator=(const IocpPool&) = delete;

  bool Start(int worker_count, Handler handler);
  void Stop();
  void* NativeHandle() const { return iocp_; }
  bool Associate(SOCKET sock, ULONG_PTR key);
  bool Post(ULONG_PTR key);
  int WorkerCount() const { return worker_count_; }

 private:
  void WorkerLoop();

  Handler handler_;
  void* iocp_{nullptr};
  std::vector<std::thread> workers_;
  int worker_count_{0};
  volatile bool stopping_{false};
};

}  // namespace pandora
