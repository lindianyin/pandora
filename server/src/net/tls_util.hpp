#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <boost/asio/ssl.hpp>

#include "common/config.hpp"
#include "common/log.hpp"

namespace pandora {

inline std::string ResolvePathMaybe(const std::string& path, const std::filesystem::path& base_dir) {
  namespace fs = std::filesystem;
  fs::path p(path);
  if (p.is_absolute()) return p.string();
  const fs::path candidates[] = {
      base_dir / p,
      base_dir.parent_path() / p,
      fs::current_path() / p,
      p,
  };
  for (const auto& c : candidates) {
    if (!c.empty() && fs::exists(c)) return fs::weakly_canonical(c).string();
  }
  return (base_dir / p).string();
}

// Returns nullptr when TLS is disabled. Throws std::runtime_error on load failure when enabled.
inline std::shared_ptr<boost::asio::ssl::context> MakeTlsContext(const NetConfig& net,
                                                                 const std::filesystem::path& base_dir = {}) {
  if (!net.tls_enabled) return nullptr;
  if (net.tls_cert.empty() || net.tls_key.empty()) {
    throw std::runtime_error("tls.enabled but cert/key path empty");
  }
  const auto cert = ResolvePathMaybe(net.tls_cert, base_dir);
  const auto key = ResolvePathMaybe(net.tls_key, base_dir);
  if (!std::filesystem::exists(cert) || !std::filesystem::exists(key)) {
    throw std::runtime_error("tls cert/key not found: cert=" + cert + " key=" + key);
  }

  auto ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls_server);
  ctx->set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
                   boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::single_dh_use);
  boost::system::error_code ec;
  ctx->use_certificate_chain_file(cert, ec);
  if (ec) throw std::runtime_error("use_certificate_chain_file: " + ec.message());
  ctx->use_private_key_file(key, boost::asio::ssl::context::pem, ec);
  if (ec) throw std::runtime_error("use_private_key_file: " + ec.message());
  PLOG_INFO("TLS context loaded cert=" << cert << " key=" << key);
  return ctx;
}

}  // namespace pandora
