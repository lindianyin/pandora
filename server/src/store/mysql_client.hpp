#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pandora {

struct MysqlRow {
  std::vector<std::string> cols;
};

class MysqlClient {
 public:
  // dsn: host=127.0.0.1;port=3306;user=pandora;password=pandora;database=pandora
  explicit MysqlClient(std::string dsn);

  bool Ping();
  bool Available() const { return available_; }

  // Returns affected rows on success, -1 on failure.
  int Exec(const std::string& sql);
  std::optional<std::vector<MysqlRow>> Query(const std::string& sql);
  std::string LastError() const { return last_error_; }

 private:
  bool ConnectAndAuth();
  void Disconnect();
  bool SendPacket(const std::vector<uint8_t>& payload, uint8_t seq);
  bool RecvPacket(std::vector<uint8_t>& payload, uint8_t& seq);
  bool RunQuery(const std::string& sql, std::vector<MysqlRow>* out_rows, int* affected);

  std::string dsn_;
  std::string host_{"127.0.0.1"};
  int port_{3306};
  std::string user_{"pandora"};
  std::string password_{"pandora"};
  std::string database_{"pandora"};
  bool available_{false};
  std::string last_error_;
  mutable std::mutex mu_;
  uintptr_t sock_{static_cast<uintptr_t>(-1)};
};

}  // namespace pandora
