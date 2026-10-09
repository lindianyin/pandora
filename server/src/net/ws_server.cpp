#include "net/ws_server.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "net/frame.hpp"
#include "net/session_hub.hpp"
#include "net/tls_util.hpp"

namespace pandora {
namespace {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

template <class Derived>
class WsSessionBase : public ISessionConn {
 public:
  explicit WsSessionBase(AuthService& auth, GameRuntime& runtime, AdminService* admin, AppConfig cfg)
      : auth_(auth), runtime_(runtime), admin_(admin), cfg_(std::move(cfg)) {}

 protected:
  Derived& self() { return static_cast<Derived&>(*this); }

  void AfterAccept(beast::error_code ec) {
    if (ec) return;
    if (!self().IsTls() && cfg_.net.force_tls) {
      Close();
      return;
    }
    if (cfg_.net.max_connections > 0 &&
        runtime_.hub.OnlineCount() >= static_cast<size_t>(cfg_.net.max_connections)) {
      Close();
      return;
    }
    last_hb_ = std::chrono::steady_clock::now();
    ArmTimer();
    DoRead();
  }

  void ArmTimer() {
    self().Timer().expires_after(std::chrono::seconds(1));
    self().Timer().async_wait([this, sp = self().Shared()](beast::error_code ec) {
      (void)sp;
      OnTimer(ec);
    });
  }

  void OnTimer(beast::error_code ec) {
    if (ec) return;
    if (std::chrono::steady_clock::now() - last_hb_ > std::chrono::seconds(cfg_.net.heartbeat_timeout_s)) {
      Send(MsgId::kS2C_Kick, proto_wire::EncodeS2C_Kick(1, "heartbeat timeout"));
      Fail();
      return;
    }
    ArmTimer();
  }

  void DoRead() {
    self().Ws().async_read(buffer_, [this, sp = self().Shared()](beast::error_code ec, std::size_t n) {
      (void)sp;
      OnRead(ec, n);
    });
  }

  void OnRead(beast::error_code ec, std::size_t) {
    if (ec) {
      Fail();
      return;
    }
    auto data = buffer_.cdata();
    const auto* p = static_cast<const uint8_t*>(data.data());
    app_buf_.insert(app_buf_.end(), p, p + data.size());
    buffer_.consume(buffer_.size());

    while (auto frame = TryDecodeOneFrame(app_buf_, cfg_.net.max_frame_bytes)) {
      last_hb_ = std::chrono::steady_clock::now();
      if (!HandleFrame(*frame)) {
        Fail();
        return;
      }
    }
    DoRead();
  }

  bool HandleFrame(const Frame& frame) {
    if (!authed_) {
      if (frame.msg_id != MsgId::kC2S_Auth) {
        Send(MsgId::kS2C_Error,
             proto_wire::EncodeS2C_Error(static_cast<int>(Err::kUnauthorized), ErrMessage(Err::kUnauthorized),
                                        frame.msg_id));
        return false;
      }
      std::string token;
      if (!proto_wire::DecodeC2S_Auth(frame.body.data(), frame.body.size(), token)) {
        Send(MsgId::kS2C_AuthResult,
             proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kBadParam), ErrMessage(Err::kBadParam), 0));
        return false;
      }
      auto sess = auth_.ValidateToken(token);
      if (!sess) {
        Send(MsgId::kS2C_AuthResult,
             proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kUnauthorized), ErrMessage(Err::kUnauthorized), 0));
        return false;
      }
      if (admin_) {
        if (admin_->IsMaintain()) {
          Send(MsgId::kS2C_AuthResult,
               proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kMaintain), ErrMessage(Err::kMaintain), 0));
          return false;
        }
        if (admin_->IsBanned(sess->uid)) {
          Send(MsgId::kS2C_AuthResult,
               proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kBanned), ErrMessage(Err::kBanned), 0));
          return false;
        }
      }
      authed_ = true;
      uid_ = sess->uid;
      runtime_.hub.Bind(uid_, self().Shared());
      Send(MsgId::kS2C_AuthResult, proto_wire::EncodeS2C_AuthResult(0, ErrMessage(Err::kOk), uid_));
      runtime_.OnReconnect(uid_);
      PLOG_INFO("ws auth ok uid=" << uid_);
      return true;
    }
    if (frame.msg_id == MsgId::kC2S_Heartbeat) {
      int64_t ts = 0;
      proto_wire::DecodeC2S_Heartbeat(frame.body.data(), frame.body.size(), ts);
      (void)ts;
      Send(MsgId::kS2C_HeartbeatAck, proto_wire::EncodeS2C_HeartbeatAck(NowMs()));
      return true;
    }
    runtime_.Dispatch(uid_, frame.msg_id, frame.body.data(), frame.body.size());
    return true;
  }

  void DoWrite() {
    self().Ws().async_write(net::buffer(write_q_.front()),
                            [this, sp = self().Shared()](beast::error_code ec, std::size_t n) {
                              (void)sp;
                              OnWrite(ec, n);
                            });
  }

  void OnWrite(beast::error_code ec, std::size_t) {
    if (ec) {
      Fail();
      return;
    }
    write_q_.pop_front();
    if (!write_q_.empty()) DoWrite();
  }

  void Fail() {
    if (closed_) return;
    closed_ = true;
    self().Timer().cancel();
    if (authed_) {
      runtime_.hub.Unbind(uid_, this);
      runtime_.OnDisconnect(uid_);
    }
    beast::error_code ignored;
    beast::get_lowest_layer(self().Ws()).shutdown(tcp::socket::shutdown_both, ignored);
  }

 public:
  void Send(uint32_t msg_id, const std::vector<uint8_t>& body) override {
    auto frame = EncodeFrame(msg_id, body);
    auto sp = self().Shared();
    net::post(self().Strand(), [sp, frame = std::move(frame)]() mutable {
      auto& d = *sp;
      d.write_q_.push_back(std::move(frame));
      if (d.write_q_.size() == 1) d.DoWrite();
    });
  }

  void Close() override {
    auto sp = self().Shared();
    net::post(self().Strand(), [sp]() { sp->Fail(); });
  }

 protected:
  beast::flat_buffer buffer_;
  std::vector<uint8_t> app_buf_;
  std::deque<std::vector<uint8_t>> write_q_;
  AuthService& auth_;
  GameRuntime& runtime_;
  AdminService* admin_;
  AppConfig cfg_;
  std::chrono::steady_clock::time_point last_hb_{};
  bool authed_{false};
  bool closed_{false};
  int64_t uid_{0};
};

class PlainWsSession : public WsSessionBase<PlainWsSession>, public std::enable_shared_from_this<PlainWsSession> {
 public:
  PlainWsSession(tcp::socket socket, AuthService& auth, GameRuntime& runtime, AdminService* admin, AppConfig cfg)
      : WsSessionBase(auth, runtime, admin, std::move(cfg)),
        ws_(std::move(socket)),
        strand_(ws_.get_executor()),
        timer_(ws_.get_executor()) {}

  void Run() {
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator([](websocket::response_type& res) {
      res.set(beast::http::field::server, "pandora-beast");
    }));
    ws_.binary(true);
    ws_.async_accept([self = shared_from_this()](beast::error_code ec) { self->AfterAccept(ec); });
  }

  bool IsTls() const { return false; }
  auto Shared() { return shared_from_this(); }
  auto& Ws() { return ws_; }
  auto& Strand() { return strand_; }
  auto& Timer() { return timer_; }

 private:
  websocket::stream<tcp::socket> ws_;
  net::strand<websocket::stream<tcp::socket>::executor_type> strand_;
  net::steady_timer timer_;
};

class SslWsSession : public WsSessionBase<SslWsSession>, public std::enable_shared_from_this<SslWsSession> {
 public:
  SslWsSession(tcp::socket socket, ssl::context& ctx, AuthService& auth, GameRuntime& runtime, AdminService* admin,
               AppConfig cfg)
      : WsSessionBase(auth, runtime, admin, std::move(cfg)),
        ws_(std::move(socket), ctx),
        strand_(ws_.get_executor()),
        timer_(ws_.get_executor()) {}

  void Run() {
    ws_.next_layer().async_handshake(ssl::stream_base::server,
                                     [self = shared_from_this()](beast::error_code ec) { self->OnSsl(ec); });
  }

  bool IsTls() const { return true; }
  auto Shared() { return shared_from_this(); }
  auto& Ws() { return ws_; }
  auto& Strand() { return strand_; }
  auto& Timer() { return timer_; }

 private:
  void OnSsl(beast::error_code ec) {
    if (ec) {
      PLOG_WARN("wss handshake failed: " << ec.message());
      return;
    }
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator([](websocket::response_type& res) {
      res.set(beast::http::field::server, "pandora-beast");
    }));
    ws_.binary(true);
    ws_.async_accept([self = shared_from_this()](beast::error_code e) { self->AfterAccept(e); });
  }

  websocket::stream<ssl::stream<tcp::socket>> ws_;
  net::strand<websocket::stream<ssl::stream<tcp::socket>>::executor_type> strand_;
  net::steady_timer timer_;
};

class WsListener : public std::enable_shared_from_this<WsListener> {
 public:
  WsListener(net::io_context& ioc, tcp::endpoint ep, AuthService& auth, GameRuntime& runtime, AdminService* admin,
             AppConfig cfg, std::shared_ptr<ssl::context> tls)
      : ioc_(ioc),
        acceptor_(net::make_strand(ioc)),
        auth_(auth),
        runtime_(runtime),
        admin_(admin),
        cfg_(std::move(cfg)),
        tls_(std::move(tls)) {
    beast::error_code ec;
    acceptor_.open(ep.protocol(), ec);
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    acceptor_.bind(ep, ec);
    if (ec) {
      PLOG_ERROR("WS bind failed: " << ec.message());
      return;
    }
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
  }

  void Run() { DoAccept(); }

 private:
  void DoAccept() {
    acceptor_.async_accept([self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
      if (ec) return;
      if (self->tls_) {
        std::make_shared<SslWsSession>(std::move(socket), *self->tls_, self->auth_, self->runtime_, self->admin_,
                                       self->cfg_)
            ->Run();
      } else {
        std::make_shared<PlainWsSession>(std::move(socket), self->auth_, self->runtime_, self->admin_, self->cfg_)
            ->Run();
      }
      self->DoAccept();
    });
  }

  net::io_context& ioc_;
  tcp::acceptor acceptor_;
  AuthService& auth_;
  GameRuntime& runtime_;
  AdminService* admin_;
  AppConfig cfg_;
  std::shared_ptr<ssl::context> tls_;
};

}  // namespace

WsServer::WsServer(AppConfig cfg, AuthService& auth, GameRuntime& runtime, AdminService* admin)
    : cfg_(std::move(cfg)), auth_(auth), runtime_(runtime), admin_(admin) {}

void WsServer::Start(boost::asio::io_context& ioc, const std::filesystem::path& conf_dir) {
  std::shared_ptr<ssl::context> tls;
  try {
    tls = MakeTlsContext(cfg_.net, conf_dir);
  } catch (const std::exception& ex) {
    PLOG_ERROR("TLS init failed: " << ex.what());
    throw;
  }
  if (cfg_.net.force_tls && !tls) {
    PLOG_ERROR("force_tls=true but tls.enabled=false; refusing plaintext WS");
    throw std::runtime_error("force_tls requires tls.enabled");
  }
  auto const address = net::ip::make_address(cfg_.net.ws_host == "0.0.0.0" ? "0.0.0.0" : cfg_.net.ws_host);
  auto listener = std::make_shared<WsListener>(ioc, tcp::endpoint{address, static_cast<unsigned short>(cfg_.net.ws_port)},
                                               auth_, runtime_, admin_, cfg_, tls);
  listener->Run();
  PLOG_INFO((tls ? "WSS" : "WS") << " Beast listening on " << cfg_.net.ws_host << ":" << cfg_.net.ws_port
                                 << " workers=" << cfg_.net.iocp_workers);
}

}  // namespace pandora
