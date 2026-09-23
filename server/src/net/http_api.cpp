#include "net/http_api.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#else
#error "http server currently targets Windows"
#endif

#include "common/errors.hpp"
#include "common/log.hpp"

namespace pandora {
namespace {

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '\\':
        o += "\\\\";
        break;
      case '"':
        o += "\\\"";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      default:
        o.push_back(c);
    }
  }
  return o;
}

std::string OkJson(const std::string& data, const std::string& trace) {
  return std::string("{\"code\":0,\"message\":\"ok\",\"data\":") + data + ",\"trace_id\":\"" + JsonEscape(trace) +
         "\"}";
}

std::string ErrJson(Err code, const std::string& trace, const std::string& msg = {}) {
  const char* m = msg.empty() ? ErrMessage(code) : msg.c_str();
  return std::string("{\"code\":") + std::to_string(static_cast<int>(code)) + ",\"message\":\"" + JsonEscape(m) +
         "\",\"data\":{},\"trace_id\":\"" + JsonEscape(trace) + "\"}";
}

std::string MakeTrace() { return std::to_string(GetTickCount64()); }

std::string ExtractJsonString(const std::string& body, const std::string& key) {
  const std::string pat = "\"" + key + "\"";
  auto p = body.find(pat);
  if (p == std::string::npos) return {};
  p = body.find(':', p);
  if (p == std::string::npos) return {};
  p = body.find('"', p);
  if (p == std::string::npos) return {};
  ++p;
  auto e = body.find('"', p);
  if (e == std::string::npos) return {};
  return body.substr(p, e - p);
}

int64_t ExtractJsonInt(const std::string& body, const std::string& key, int64_t def = 0) {
  const std::string pat = "\"" + key + "\"";
  auto p = body.find(pat);
  if (p == std::string::npos) return def;
  p = body.find(':', p);
  if (p == std::string::npos) return def;
  ++p;
  while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
  try {
    return std::stoll(body.substr(p));
  } catch (...) {
    return def;
  }
}

bool ExtractJsonBool(const std::string& body, const std::string& key, bool def = false) {
  const std::string pat = "\"" + key + "\"";
  auto p = body.find(pat);
  if (p == std::string::npos) return def;
  p = body.find(':', p);
  if (p == std::string::npos) return def;
  auto sub = body.substr(p + 1, 16);
  if (sub.find("true") != std::string::npos) return true;
  if (sub.find("false") != std::string::npos) return false;
  return def;
}

std::string ExtractBearer(const std::string& req) {
  const auto auth_h = req.find("Authorization:");
  if (auth_h == std::string::npos) return {};
  auto p = req.find("Bearer ", auth_h);
  if (p == std::string::npos) return {};
  p += 7;
  auto e = req.find("\r\n", p);
  auto t = req.substr(p, e == std::string::npos ? std::string::npos : e - p);
  while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) t.pop_back();
  return t;
}

std::string HttpResponse(int status, const std::string& body) {
  std::ostringstream oss;
  oss << "HTTP/1.1 " << status << " OK\r\n"
      << "Content-Type: application/json; charset=utf-8\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
      << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
      << "Connection: close\r\n"
      << "Content-Length: " << body.size() << "\r\n\r\n"
      << body;
  return oss.str();
}

int64_t PathUid(const std::string& path, const std::string& prefix) {
  if (path.rfind(prefix, 0) != 0) return 0;
  auto rest = path.substr(prefix.size());
  auto slash = rest.find('/');
  try {
    return std::stoll(slash == std::string::npos ? rest : rest.substr(0, slash));
  } catch (...) {
    return 0;
  }
}

void HandleClient(socket_t client, MemoryStore& store, AuthService& auth, WalletService& wallet, PayService& pay,
                  AdminService& admin, ActivityService& activity, MysqlClient& mysql, RedisClient& redis,
                  const AppConfig& cfg) {
  char buf[16384];
  const int n = recv(client, buf, sizeof(buf) - 1, 0);
  if (n <= 0) {
    closesocket(client);
    return;
  }
  buf[n] = 0;
  const std::string req(buf, n);
  const auto line_end = req.find("\r\n");
  const std::string request_line = line_end == std::string::npos ? req : req.substr(0, line_end);
  std::string method, path;
  {
    std::istringstream iss(request_line);
    iss >> method >> path;
  }
  std::string body;
  const auto hdr_end = req.find("\r\n\r\n");
  if (hdr_end != std::string::npos) body = req.substr(hdr_end + 4);

  if (method == "OPTIONS") {
    const auto resp = HttpResponse(204, "");
    send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
    closesocket(client);
    return;
  }

  const auto trace = MakeTrace();
  std::string resp_body;
  int status = 200;

  auto requirePlayer = [&]() -> std::optional<int64_t> {
    auto sess = auth.ValidateToken(ExtractBearer(req));
    if (!sess) return std::nullopt;
    return sess->uid;
  };

  auto requireAdmin = [&](AdminRole min) -> std::optional<AdminSession> {
    auto s = admin.Validate(ExtractBearer(req));
    if (!s || !admin.RequireRole(*s, min)) return std::nullopt;
    return s;
  };

  if (method == "GET" && path == "/health") {
    const bool mysql_ok = mysql.Ping();
    const bool redis_ok = redis.Ping();
    resp_body = OkJson(std::string("{\"status\":\"ok\",\"mysql\":") + (mysql_ok ? "true" : "false") + ",\"redis\":" +
                           (redis_ok ? "true" : "false") + ",\"store\":\"" + cfg.store_backend +
                           "\",\"ccu\":" + std::to_string(store.SessionCount()) + "}",
                       trace);
  } else if (method == "GET" && path == "/admin/v1/ping") {
    resp_body = OkJson("{\"pong\":true}", trace);
  } else if (method == "POST" && path.rfind("/api/v1/auth/login", 0) == 0) {
    if (admin.IsMaintain()) {
      status = 503;
      resp_body = ErrJson(Err::kMaintain, trace);
    } else {
      const auto device = ExtractJsonString(body, "device_id");
      auto login = auth.LoginGuest(device);
      if (admin.IsBanned(login.uid)) {
        status = 403;
        resp_body = ErrJson(Err::kBanned, trace);
      } else {
        try {
          activity.OnLogin(login.uid);
        } catch (...) {
        }
        std::ostringstream data;
        data << "{\"access_token\":\"" << JsonEscape(login.token) << "\",\"uid\":" << login.uid << ",\"nickname\":\""
             << JsonEscape(login.nickname) << "\",\"gold\":" << login.gold << ",\"diamond\":" << login.diamond << "}";
        resp_body = OkJson(data.str(), trace);
      }
    }
  } else if (method == "GET" && path.rfind("/api/v1/player/profile", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else {
      auto player = wallet.Profile(*uid);
      if (!player) {
        status = 404;
        resp_body = ErrJson(Err::kNotFound, trace);
      } else {
        std::ostringstream data;
        data << "{\"uid\":" << player->uid << ",\"nickname\":\"" << JsonEscape(player->nickname)
             << "\",\"gold\":" << player->gold << ",\"diamond\":" << player->diamond << ",\"level\":1}";
        resp_body = OkJson(data.str(), trace);
      }
    }
  } else if (method == "POST" && path.rfind("/api/v1/wallet/exchange", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else {
      const int64_t diamond = ExtractJsonInt(body, "diamond", 0);
      const auto order_id = ExtractJsonString(body, "client_order_id");
      auto r = wallet.ExchangeDiamondToGold(*uid, diamond, order_id);
      if (!r.ok) {
        status = 400;
        resp_body = ErrJson(Err::kInsufficient, trace, r.error);
      } else {
        auto p = wallet.Profile(*uid);
        std::ostringstream data;
        data << "{\"gold\":" << (p ? p->gold : r.balance) << ",\"diamond\":" << (p ? p->diamond : 0)
             << ",\"rate\":" << wallet.DiamondToGoldRate() << "}";
        resp_body = OkJson(data.str(), trace);
      }
    }
  } else if (method == "GET" && path.rfind("/api/v1/pay/products", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else {
      auto products = pay.ListProducts(false);
      std::ostringstream data;
      data << "{\"items\":[";
      for (size_t i = 0; i < products.size(); ++i) {
        if (i) data << ",";
        data << "{\"id\":" << products[i].id << ",\"amount_fen\":" << products[i].amount_fen
             << ",\"diamond\":" << products[i].diamond << ",\"gift_diamond\":" << products[i].gift_diamond << "}";
      }
      data << "],\"sandbox\":" << (pay.Sandbox() ? "true" : "false") << "}";
      resp_body = OkJson(data.str(), trace);
    }
  } else if (method == "POST" && path.rfind("/api/v1/pay/alipay/create", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else {
      const int product_id = static_cast<int>(ExtractJsonInt(body, "product_id", 0));
      auto order = pay.CreateOrder(*uid, product_id);
      if (!order) {
        status = 400;
        resp_body = ErrJson(Err::kBadParam, trace, "bad product");
      } else {
        const auto ostr = pay.BuildOrderStr(*order);
        std::ostringstream data;
        data << "{\"order_id\":\"" << JsonEscape(order->order_id) << "\",\"amount_fen\":" << order->amount_fen
             << ",\"diamond\":" << order->diamond << ",\"alipay_order_str\":\"" << JsonEscape(ostr)
             << "\",\"sandbox\":" << (pay.Sandbox() ? "true" : "false") << "}";
        resp_body = OkJson(data.str(), trace);
      }
    }
  } else if (method == "POST" && path.rfind("/api/v1/pay/alipay/notify", 0) == 0) {
    const auto order_id = ExtractJsonString(body, "out_trade_no");
    const auto trade_no = ExtractJsonString(body, "trade_no");
    const int amount = static_cast<int>(ExtractJsonInt(body, "total_amount", 0));
    const bool ok = pay.HandleNotify(order_id, trade_no, amount);
    resp_body = ok ? "success" : "fail";
  } else if (method == "POST" && path.rfind("/api/v1/pay/alipay/sandbox_complete", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else if (!pay.Sandbox()) {
      status = 403;
      resp_body = ErrJson(Err::kForbidden, trace, "sandbox disabled");
    } else {
      const auto order_id = ExtractJsonString(body, "order_id");
      const bool ok = pay.SandboxComplete(*uid, order_id);
      if (!ok) {
        status = 400;
        resp_body = ErrJson(Err::kBadParam, trace, "complete failed");
      } else {
        resp_body = OkJson("{\"ok\":true}", trace);
      }
    }
  } else if (method == "GET" && path == "/api/v1/activity/list") {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else {
      resp_body = OkJson(activity.ListForPlayerJson(*uid), trace);
    }
  } else if (method == "GET" && path.rfind("/api/v1/activity/", 0) == 0 && path.find("/progress") != std::string::npos) {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else {
      const int aid = static_cast<int>(PathUid(path, "/api/v1/activity/"));
      resp_body = OkJson(activity.ProgressJson(*uid, aid), trace);
    }
  } else if (method == "POST" && path.rfind("/api/v1/activity/", 0) == 0 && path.find("/claim") != std::string::npos) {
    auto uid = requirePlayer();
    if (!uid) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace);
    } else {
      const int aid = static_cast<int>(PathUid(path, "/api/v1/activity/"));
      const auto rk = ExtractJsonString(body, "reward_key");
      auto cr = activity.Claim(*uid, aid, rk);
      if (!cr.ok) {
        status = 400;
        resp_body = ErrJson(Err::kActivityCannotClaim, trace, cr.error);
      } else {
        resp_body = OkJson("{\"balance\":" + std::to_string(cr.balance) + ",\"currency\":" +
                               std::to_string(cr.currency) + "}",
                           trace);
      }
    }
  }
  // -------- Admin API --------
  else if (method == "POST" && path.rfind("/admin/v1/auth/login", 0) == 0) {
    const auto user = ExtractJsonString(body, "username");
    const auto pass = ExtractJsonString(body, "password");
    auto r = admin.Login(user, pass);
    if (!r.ok) {
      status = 401;
      resp_body = ErrJson(Err::kUnauthorized, trace, r.error);
    } else {
      std::ostringstream data;
      data << "{\"access_token\":\"" << JsonEscape(r.token) << "\",\"username\":\"" << JsonEscape(r.username)
           << "\",\"role\":\"" << r.role << "\"}";
      resp_body = OkJson(data.str(), trace);
    }
  } else if (path.rfind("/admin/v1/", 0) == 0) {
    // Player token must not access admin
    if (auth.ValidateToken(ExtractBearer(req)) && !admin.Validate(ExtractBearer(req))) {
      status = 403;
      resp_body = ErrJson(Err::kForbidden, trace, "player token not allowed");
    } else if (method == "GET" && path == "/admin/v1/dashboard") {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else
        resp_body = OkJson(admin.DashboardJson(), trace);
    } else if (method == "GET" && path.rfind("/admin/v1/players", 0) == 0 &&
               path.find("/kick") == std::string::npos && path.find("/ban") == std::string::npos &&
               path.find("/unban") == std::string::npos) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const auto q = ExtractJsonString(body, "q");
        // also support query string ?q=
        std::string qq = q;
        auto qp = path.find("?q=");
        if (qp != std::string::npos) qq = path.substr(qp + 3);
        resp_body = OkJson(admin.ListPlayersJson(qq, 1, 50), trace);
      }
    } else if (method == "POST" && path.find("/admin/v1/players/") == 0 && path.find("/kick") != std::string::npos) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int64_t uid = PathUid(path, "/admin/v1/players/");
        std::string err;
        if (!admin.Kick(uid, *s, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else
          resp_body = OkJson("{\"ok\":true}", trace);
      }
    } else if (method == "POST" && path.find("/admin/v1/players/") == 0 && path.find("/ban") != std::string::npos &&
               path.find("/unban") == std::string::npos) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int64_t uid = PathUid(path, "/admin/v1/players/");
        std::string err;
        if (!admin.Ban(uid, true, *s, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else
          resp_body = OkJson("{\"ok\":true}", trace);
      }
    } else if (method == "POST" && path.find("/admin/v1/players/") == 0 && path.find("/unban") != std::string::npos) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int64_t uid = PathUid(path, "/admin/v1/players/");
        std::string err;
        if (!admin.Ban(uid, false, *s, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else
          resp_body = OkJson("{\"ok\":true}", trace);
      }
    } else if (method == "POST" && path == "/admin/v1/wallet/adjust") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int64_t uid = ExtractJsonInt(body, "uid", 0);
        const int currency = static_cast<int>(ExtractJsonInt(body, "currency", 1));
        const int64_t delta = ExtractJsonInt(body, "delta", 0);
        const auto idem = ExtractJsonString(body, "idempotent_key");
        std::string err;
        int64_t bal = 0;
        if (!admin.WalletAdjust(uid, currency, delta, idem, *s, &err, &bal)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else {
          resp_body = OkJson("{\"balance\":" + std::to_string(bal) + "}", trace);
        }
      }
    } else if (method == "GET" && path.rfind("/admin/v1/wallet/ledgers", 0) == 0) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int64_t uid = ExtractJsonInt(body, "uid", 0);
        resp_body = OkJson(admin.ListLedgersJson(uid, 1, 50), trace);
      }
    } else if (method == "GET" && path.rfind("/admin/v1/rounds", 0) == 0) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int64_t uid = ExtractJsonInt(body, "uid", 0);
        resp_body = OkJson(admin.ListRoundsJson(uid, 1, 50), trace);
      }
    } else if ((method == "GET" || method == "PUT") && path.rfind("/admin/v1/rooms/templates", 0) == 0) {
      if (method == "GET") {
        auto s = requireAdmin(AdminRole::kCs);
        if (!s) {
          status = 403;
          resp_body = ErrJson(Err::kForbidden, trace);
        } else
          resp_body = OkJson(admin.ListTemplatesJson(), trace);
      } else {
        auto s = requireAdmin(AdminRole::kOps);
        if (!s) {
          status = 403;
          resp_body = ErrJson(Err::kForbidden, trace);
        } else {
          std::string err;
          const bool ok =
              admin.PutTemplate(static_cast<int>(ExtractJsonInt(body, "id", 1)), ExtractJsonString(body, "name"),
                                static_cast<int>(ExtractJsonInt(body, "base_score", 100)),
                                static_cast<int>(ExtractJsonInt(body, "rake_bp", 500)),
                                ExtractJsonInt(body, "min_gold", 0), ExtractJsonInt(body, "max_gold", 0),
                                ExtractJsonBool(body, "enabled", true), *s, &err);
          if (!ok) {
            status = 400;
            resp_body = ErrJson(Err::kBadParam, trace, err);
          } else
            resp_body = OkJson("{\"ok\":true}", trace);
        }
      }
    } else if (method == "GET" && path == "/admin/v1/pay/products") {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else
        resp_body = OkJson(admin.ListProductsJson(), trace);
    } else if (method == "POST" && path == "/admin/v1/pay/products") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        std::string err;
        const bool ok = admin.UpsertProduct(static_cast<int>(ExtractJsonInt(body, "id", 0)),
                                           static_cast<int>(ExtractJsonInt(body, "amount_fen", 0)),
                                           static_cast<int>(ExtractJsonInt(body, "diamond", 0)),
                                           static_cast<int>(ExtractJsonInt(body, "gift_diamond", 0)),
                                           ExtractJsonBool(body, "enabled", true), *s, &err);
        if (!ok) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else
          resp_body = OkJson("{\"ok\":true}", trace);
      }
    } else if (method == "DELETE" && path.rfind("/admin/v1/pay/products/", 0) == 0) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int id = static_cast<int>(PathUid(path, "/admin/v1/pay/products/"));
        std::string err;
        if (!admin.DeleteProduct(id, *s, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else
          resp_body = OkJson("{\"ok\":true}", trace);
      }
    } else if (method == "GET" && path.rfind("/admin/v1/pay/orders", 0) == 0) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else
        resp_body = OkJson(admin.ListOrdersJson(1, 50), trace);
    } else if (method == "POST" && path == "/admin/v1/announce") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        std::string err;
        if (!admin.Announce(ExtractJsonString(body, "message"), *s, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else
          resp_body = OkJson("{\"ok\":true}", trace);
      }
    } else if (method == "POST" && path == "/admin/v1/ops/maintain") {
      auto s = requireAdmin(AdminRole::kSuper);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        std::string err;
        if (!admin.SetMaintainOp(ExtractJsonBool(body, "enabled", false), *s, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else
          resp_body = OkJson("{\"ok\":true}", trace);
      }
    } else if (method == "GET" && path.rfind("/admin/v1/audit", 0) == 0) {
      auto s = requireAdmin(AdminRole::kSuper);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else
        resp_body = OkJson(admin.ListAuditJson(1, 50), trace);
    } else if (method == "GET" && path == "/admin/v1/activities") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        auto defs = activity.ListDefs(true);
        std::ostringstream data;
        data << "{\"items\":[";
        for (size_t i = 0; i < defs.size(); ++i) {
          if (i) data << ",";
          data << "{\"id\":" << defs[i].id << ",\"type\":\"" << JsonEscape(defs[i].type) << "\",\"title\":\""
               << JsonEscape(defs[i].title) << "\",\"rules_json\":" << defs[i].rules_json
               << ",\"enabled\":" << (defs[i].enabled ? "true" : "false")
               << ",\"claim_count\":" << activity.ClaimCount(defs[i].id) << "}";
        }
        data << "]}";
        resp_body = OkJson(data.str(), trace);
      }
    } else if ((method == "POST" || method == "PUT") && path == "/admin/v1/activities") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        ActivityDef d;
        d.id = static_cast<int>(ExtractJsonInt(body, "id", 0));
        d.type = ExtractJsonString(body, "type");
        d.title = ExtractJsonString(body, "title");
        // rules_json may be object — extract raw substring roughly
        auto rp = body.find("\"rules_json\"");
        if (rp != std::string::npos) {
          auto colon = body.find(':', rp);
          auto start = body.find_first_not_of(" \t", colon + 1);
          if (start != std::string::npos && body[start] == '{') {
            int depth = 0;
            size_t i = start;
            for (; i < body.size(); ++i) {
              if (body[i] == '{') ++depth;
              else if (body[i] == '}') {
                --depth;
                if (depth == 0) {
                  d.rules_json = body.substr(start, i - start + 1);
                  break;
                }
              }
            }
          } else {
            d.rules_json = ExtractJsonString(body, "rules_json");
            if (!d.rules_json.empty() && d.rules_json[0] != '{') d.rules_json = "{}";
          }
        }
        if (d.rules_json.empty()) d.rules_json = "{}";
        d.enabled = ExtractJsonBool(body, "enabled", true);
        std::string err;
        if (!activity.UpsertDef(d, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else {
          admin.Audit(s->admin_id, "upsert_activity", d.title, "", d.type);
          resp_body = OkJson("{\"ok\":true}", trace);
        }
      }
    } else if (method == "POST" && path == "/admin/v1/activities/simulate_settle") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int64_t uid = ExtractJsonInt(body, "uid", 0);
        const int tid = static_cast<int>(ExtractJsonInt(body, "template_id", 1));
        if (uid <= 0) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, "uid required");
        } else {
          try {
            activity.OnGameSettled(uid, tid);
          } catch (...) {
          }
          admin.Audit(s->admin_id, "simulate_settle", std::to_string(uid), "", std::to_string(tid));
          resp_body = OkJson("{\"ok\":true}", trace);
        }
      }
    } else if (method == "DELETE" && path.rfind("/admin/v1/activities/", 0) == 0) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) {
        status = 403;
        resp_body = ErrJson(Err::kForbidden, trace);
      } else {
        const int id = static_cast<int>(PathUid(path, "/admin/v1/activities/"));
        std::string err;
        if (!activity.SetEnabled(id, false, &err)) {
          status = 400;
          resp_body = ErrJson(Err::kBadParam, trace, err);
        } else {
          admin.Audit(s->admin_id, "disable_activity", std::to_string(id), "", "0");
          resp_body = OkJson("{\"ok\":true}", trace);
        }
      }
    } else {
      status = 404;
      resp_body = ErrJson(Err::kNotFound, trace);
    }
  } else {
    status = 404;
    resp_body = ErrJson(Err::kNotFound, trace);
  }

  const auto resp = HttpResponse(status, resp_body);
  send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
  closesocket(client);
}

}  // namespace

HttpApi::HttpApi(AppConfig cfg, MemoryStore& store, AuthService& auth, WalletService& wallet, PayService& pay,
                 AdminService& admin, ActivityService& activity, MysqlClient& mysql, RedisClient& redis)
    : cfg_(std::move(cfg)),
      store_(store),
      auth_(auth),
      wallet_(wallet),
      pay_(pay),
      admin_(admin),
      activity_(activity),
      mysql_(mysql),
      redis_(redis) {}

void HttpApi::Run() {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  socket_t listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  BOOL yes = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(cfg_.net.http_port));
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    PLOG_ERROR("http bind failed port=" << cfg_.net.http_port);
    return;
  }
  listen(listen_sock, 128);
  PLOG_INFO("HTTP listening on :" << cfg_.net.http_port);
  while (true) {
    socket_t client = accept(listen_sock, nullptr, nullptr);
    if (client == INVALID_SOCKET) continue;
    std::thread(HandleClient, client, std::ref(store_), std::ref(auth_), std::ref(wallet_), std::ref(pay_),
                std::ref(admin_), std::ref(activity_), std::ref(mysql_), std::ref(redis_), std::cref(cfg_))
        .detach();
  }
}

}  // namespace pandora

