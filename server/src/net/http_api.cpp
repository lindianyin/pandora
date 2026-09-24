#include "net/http_api.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <nlohmann/json.hpp>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "net/tls_util.hpp"

namespace pandora {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

std::string MakeTrace() {
  using namespace std::chrono;
  return std::to_string(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

json OkObj(json data, const std::string& trace) {
  return json{{"code", 0}, {"message", "ok"}, {"data", std::move(data)}, {"trace_id", trace}};
}

json ErrObj(Err code, const std::string& trace, const std::string& msg = {}) {
  return json{{"code", static_cast<int>(code)},
              {"message", msg.empty() ? ErrMessage(code) : msg},
              {"data", json::object()},
              {"trace_id", trace}};
}

json ParseBody(const std::string& body) {
  if (body.empty()) return json::object();
  try {
    return json::parse(body);
  } catch (...) {
    return json::object();
  }
}

std::string JStr(const json& j, const char* key, const std::string& def = {}) {
  if (!j.contains(key)) return def;
  if (j[key].is_string()) return j[key].get<std::string>();
  if (j[key].is_number() || j[key].is_boolean()) return j[key].dump();
  return def;
}

int64_t JInt(const json& j, const char* key, int64_t def = 0) {
  if (!j.contains(key) || j[key].is_null()) return def;
  try {
    if (j[key].is_number_integer()) return j[key].get<int64_t>();
    if (j[key].is_string()) return std::stoll(j[key].get<std::string>());
  } catch (...) {
  }
  return def;
}

bool JBool(const json& j, const char* key, bool def = false) {
  if (!j.contains(key)) return def;
  if (j[key].is_boolean()) return j[key].get<bool>();
  return def;
}

std::string ExtractBearer(const http::request<http::string_body>& req) {
  auto it = req.find(http::field::authorization);
  if (it == req.end()) return {};
  std::string v(it->value());
  const std::string prefix = "Bearer ";
  if (v.rfind(prefix, 0) != 0) return {};
  return v.substr(prefix.size());
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

struct HttpResult {
  int status{200};
  std::string content_type{"application/json; charset=utf-8"};
  std::string body;
  std::string content_disposition;
};

HttpResult Dispatch(const http::request<http::string_body>& req, MemoryStore& /*store*/, AuthService& auth,
                    WalletService& wallet, PayService& pay, AdminService& admin, ActivityService& activity,
                    MysqlClient& mysql, RedisClient& redis, SessionHub& hub, const AppConfig& cfg) {
  HttpResult out;
  const std::string method(req.method_string());
  std::string target(req.target());
  std::string path = target;
  std::string query;
  const auto qpos = path.find('?');
  if (qpos != std::string::npos) {
    query = path.substr(qpos + 1);
    path = path.substr(0, qpos);
  }
  const auto body_str = req.body();
  const json body = ParseBody(body_str);
  const auto trace = MakeTrace();
  const auto bearer = ExtractBearer(req);

  auto requirePlayer = [&]() -> std::optional<int64_t> {
    auto sess = auth.ValidateToken(bearer);
    if (!sess) return std::nullopt;
    return sess->uid;
  };
  auto requireAdmin = [&](AdminRole min) -> std::optional<AdminSession> {
    auto s = admin.Validate(bearer);
    if (!s || !admin.RequireRole(*s, min)) return std::nullopt;
    return s;
  };
  auto setJson = [&](int status, const json& j) {
    out.status = status;
    out.body = j.dump();
  };

  if (method == "OPTIONS") {
    out.status = 204;
    out.body.clear();
    return out;
  }

  if (method == "GET" && path == "/health") {
    setJson(200, OkObj({{"status", "ok"},
                        {"mysql", mysql.Ping()},
                        {"redis", redis.Ping()},
                        {"store", cfg.store_backend},
                        {"ccu", hub.OnlineCount()},
                        {"http_workers", cfg.worker.biz_threads},
                        {"ws_workers", cfg.net.iocp_workers},
                        {"async_workers", cfg.worker.async_threads},
                        {"iocp_workers", cfg.net.iocp_workers},
                        {"tls", cfg.net.tls_enabled}},
                       trace));
  } else if (method == "GET" && path == "/admin/v1/ping") {
    setJson(200, OkObj({{"pong", true}}, trace));
  } else if (method == "POST" && path.rfind("/api/v1/auth/login", 0) == 0) {
    if (admin.IsMaintain()) {
      setJson(503, ErrObj(Err::kMaintain, trace));
    } else {
      auto login = auth.LoginGuest(JStr(body, "device_id"));
      if (admin.IsBanned(login.uid)) {
        setJson(403, ErrObj(Err::kBanned, trace));
      } else {
        try {
          activity.OnLogin(login.uid);
        } catch (...) {
        }
        setJson(200, OkObj({{"access_token", login.token},
                            {"uid", login.uid},
                            {"nickname", login.nickname},
                            {"gold", login.gold},
                            {"diamond", login.diamond}},
                           trace));
      }
    }
  } else if (method == "GET" && path.rfind("/api/v1/player/profile", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else {
      auto player = wallet.Profile(*uid);
      if (!player) setJson(404, ErrObj(Err::kNotFound, trace));
      else
        setJson(200, OkObj({{"uid", player->uid},
                            {"nickname", player->nickname},
                            {"gold", player->gold},
                            {"diamond", player->diamond},
                            {"level", 1}},
                           trace));
    }
  } else if (method == "POST" && path.rfind("/api/v1/wallet/exchange", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else {
      auto r = wallet.ExchangeDiamondToGold(*uid, JInt(body, "diamond"), JStr(body, "client_order_id"));
      if (!r.ok) setJson(400, ErrObj(Err::kInsufficient, trace, r.error));
      else {
        auto p = wallet.Profile(*uid);
        setJson(200, OkObj({{"gold", p ? p->gold : r.balance},
                            {"diamond", p ? p->diamond : 0},
                            {"rate", wallet.DiamondToGoldRate()}},
                           trace));
      }
    }
  } else if (method == "GET" && path.rfind("/api/v1/pay/products", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else {
      json items = json::array();
      for (const auto& p : pay.ListProducts(false)) {
        items.push_back({{"id", p.id},
                         {"amount_fen", p.amount_fen},
                         {"diamond", p.diamond},
                         {"gift_diamond", p.gift_diamond}});
      }
      setJson(200, OkObj({{"items", items}, {"sandbox", pay.Sandbox()}}, trace));
    }
  } else if (method == "POST" && path.rfind("/api/v1/pay/alipay/create", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else {
      auto order = pay.CreateOrder(*uid, static_cast<int>(JInt(body, "product_id")));
      if (!order) setJson(400, ErrObj(Err::kBadParam, trace, "bad product"));
      else
        setJson(200, OkObj({{"order_id", order->order_id},
                            {"amount_fen", order->amount_fen},
                            {"diamond", order->diamond},
                            {"alipay_order_str", pay.BuildOrderStr(*order)},
                            {"sandbox", pay.Sandbox()}},
                           trace));
    }
  } else if (method == "POST" && path.rfind("/api/v1/pay/alipay/notify", 0) == 0) {
    const bool ok =
        pay.HandleNotify(JStr(body, "out_trade_no"), JStr(body, "trade_no"), static_cast<int>(JInt(body, "total_amount")));
    out.status = 200;
    out.content_type = "text/plain; charset=utf-8";
    out.body = ok ? "success" : "fail";
  } else if (method == "POST" && path.rfind("/api/v1/pay/alipay/sandbox_complete", 0) == 0) {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else if (!pay.Sandbox()) setJson(403, ErrObj(Err::kForbidden, trace, "sandbox disabled"));
    else if (!pay.SandboxComplete(*uid, JStr(body, "order_id")))
      setJson(400, ErrObj(Err::kBadParam, trace, "complete failed"));
    else
      setJson(200, OkObj({{"ok", true}}, trace));
  } else if (method == "GET" && path == "/api/v1/activity/list") {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else setJson(200, OkObj(json::parse(activity.ListForPlayerJson(*uid), nullptr, false), trace));
  } else if (method == "GET" && path.rfind("/api/v1/activity/", 0) == 0 && path.find("/progress") != std::string::npos) {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else {
      const int aid = static_cast<int>(PathUid(path, "/api/v1/activity/"));
      setJson(200, OkObj(json::parse(activity.ProgressJson(*uid, aid), nullptr, false), trace));
    }
  } else if (method == "POST" && path.rfind("/api/v1/activity/", 0) == 0 && path.find("/claim") != std::string::npos) {
    auto uid = requirePlayer();
    if (!uid) setJson(401, ErrObj(Err::kUnauthorized, trace));
    else {
      const int aid = static_cast<int>(PathUid(path, "/api/v1/activity/"));
      auto cr = activity.Claim(*uid, aid, JStr(body, "reward_key"));
      if (!cr.ok) setJson(400, ErrObj(Err::kActivityCannotClaim, trace, cr.error));
      else setJson(200, OkObj({{"balance", cr.balance}, {"currency", cr.currency}}, trace));
    }
  } else if (method == "POST" && path.rfind("/admin/v1/auth/login", 0) == 0) {
    auto r = admin.Login(JStr(body, "username"), JStr(body, "password"));
    if (!r.ok) setJson(401, ErrObj(Err::kUnauthorized, trace, r.error));
    else setJson(200, OkObj({{"access_token", r.token}, {"username", r.username}, {"role", r.role}}, trace));
  } else if (path.rfind("/admin/v1/", 0) == 0) {
    if (auth.ValidateToken(bearer) && !admin.Validate(bearer)) {
      setJson(403, ErrObj(Err::kForbidden, trace, "player token not allowed"));
      return out;
    }
    if (method == "GET" && path == "/admin/v1/dashboard") {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else setJson(200, OkObj(json::parse(admin.DashboardJson(), nullptr, false), trace));
    } else if (method == "GET" && path.rfind("/admin/v1/players", 0) == 0 && path.find("/kick") == std::string::npos &&
               path.find("/ban") == std::string::npos && path.find("/unban") == std::string::npos) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string qq = JStr(body, "q");
        auto qp = query.find("q=");
        if (qp != std::string::npos) qq = query.substr(qp + 2);
        setJson(200, OkObj(json::parse(admin.ListPlayersJson(qq, 1, 50), nullptr, false), trace));
      }
    } else if (method == "POST" && path.find("/admin/v1/players/") == 0 && path.find("/kick") != std::string::npos) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        if (!admin.Kick(PathUid(path, "/admin/v1/players/"), *s, &err))
          setJson(400, ErrObj(Err::kBadParam, trace, err));
        else
          setJson(200, OkObj({{"ok", true}}, trace));
      }
    } else if (method == "POST" && path.find("/admin/v1/players/") == 0 && path.find("/ban") != std::string::npos &&
               path.find("/unban") == std::string::npos) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        if (!admin.Ban(PathUid(path, "/admin/v1/players/"), true, *s, &err))
          setJson(400, ErrObj(Err::kBadParam, trace, err));
        else
          setJson(200, OkObj({{"ok", true}}, trace));
      }
    } else if (method == "POST" && path.find("/admin/v1/players/") == 0 && path.find("/unban") != std::string::npos) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        if (!admin.Ban(PathUid(path, "/admin/v1/players/"), false, *s, &err))
          setJson(400, ErrObj(Err::kBadParam, trace, err));
        else
          setJson(200, OkObj({{"ok", true}}, trace));
      }
    } else if (method == "POST" && path == "/admin/v1/wallet/adjust") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        int64_t bal = 0;
        if (!admin.WalletAdjust(JInt(body, "uid"), static_cast<int>(JInt(body, "currency", 1)), JInt(body, "delta"),
                                JStr(body, "idempotent_key"), *s, &err, &bal))
          setJson(400, ErrObj(Err::kBadParam, trace, err));
        else
          setJson(200, OkObj({{"balance", bal}}, trace));
      }
    } else if (method == "GET" && path.rfind("/admin/v1/wallet/ledgers", 0) == 0) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else setJson(200, OkObj(json::parse(admin.ListLedgersJson(JInt(body, "uid"), 1, 50), nullptr, false), trace));
    } else if (method == "GET" && path.rfind("/admin/v1/rounds", 0) == 0) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else setJson(200, OkObj(json::parse(admin.ListRoundsJson(JInt(body, "uid"), 1, 50), nullptr, false), trace));
    } else if ((method == "GET" || method == "PUT") && path.rfind("/admin/v1/rooms/templates", 0) == 0) {
      if (method == "GET") {
        auto s = requireAdmin(AdminRole::kCs);
        if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
        else setJson(200, OkObj(json::parse(admin.ListTemplatesJson(), nullptr, false), trace));
      } else {
        auto s = requireAdmin(AdminRole::kOps);
        if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
        else {
          std::string err;
          const bool ok = admin.PutTemplate(static_cast<int>(JInt(body, "id", 1)), JStr(body, "name"),
                                            static_cast<int>(JInt(body, "base_score", 100)),
                                            static_cast<int>(JInt(body, "rake_bp", 500)), JInt(body, "min_gold"),
                                            JInt(body, "max_gold"), JBool(body, "enabled", true), *s, &err);
          if (!ok) setJson(400, ErrObj(Err::kBadParam, trace, err));
          else setJson(200, OkObj({{"ok", true}}, trace));
        }
      }
    } else if (method == "GET" && path == "/admin/v1/pay/products") {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else setJson(200, OkObj(json::parse(admin.ListProductsJson(), nullptr, false), trace));
    } else if (method == "POST" && path == "/admin/v1/pay/products") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        const bool ok = admin.UpsertProduct(static_cast<int>(JInt(body, "id")), static_cast<int>(JInt(body, "amount_fen")),
                                           static_cast<int>(JInt(body, "diamond")),
                                           static_cast<int>(JInt(body, "gift_diamond")), JBool(body, "enabled", true),
                                           *s, &err);
        if (!ok) setJson(400, ErrObj(Err::kBadParam, trace, err));
        else setJson(200, OkObj({{"ok", true}}, trace));
      }
    } else if (method == "DELETE" && path.rfind("/admin/v1/pay/products/", 0) == 0) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        if (!admin.DeleteProduct(static_cast<int>(PathUid(path, "/admin/v1/pay/products/")), *s, &err))
          setJson(400, ErrObj(Err::kBadParam, trace, err));
        else
          setJson(200, OkObj({{"ok", true}}, trace));
      }
    } else if (method == "GET" && path.rfind("/admin/v1/pay/orders", 0) == 0) {
      auto s = requireAdmin(AdminRole::kCs);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else setJson(200, OkObj(json::parse(admin.ListOrdersJson(1, 50), nullptr, false), trace));
    } else if (method == "POST" && path == "/admin/v1/announce") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        if (!admin.Announce(JStr(body, "message"), *s, &err)) setJson(400, ErrObj(Err::kBadParam, trace, err));
        else setJson(200, OkObj({{"ok", true}}, trace));
      }
    } else if (method == "POST" && path == "/admin/v1/ops/maintain") {
      auto s = requireAdmin(AdminRole::kSuper);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        std::string err;
        if (!admin.SetMaintainOp(JBool(body, "enabled"), *s, &err)) setJson(400, ErrObj(Err::kBadParam, trace, err));
        else setJson(200, OkObj({{"ok", true}}, trace));
      }
    } else if (method == "GET" && path.rfind("/admin/v1/audit", 0) == 0) {
      auto s = requireAdmin(AdminRole::kSuper);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else setJson(200, OkObj(json::parse(admin.ListAuditJson(1, 50), nullptr, false), trace));
    } else if (method == "GET" && path == "/admin/v1/activities") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        json items = json::array();
        for (const auto& d : activity.ListDefs(true)) {
          items.push_back({{"id", d.id},
                           {"type", d.type},
                           {"title", d.title},
                           {"rules_json", json::parse(d.rules_json, nullptr, false)},
                           {"enabled", d.enabled},
                           {"claim_count", activity.ClaimCount(d.id)}});
        }
        setJson(200, OkObj({{"items", items}}, trace));
      }
    } else if ((method == "POST" || method == "PUT") && path == "/admin/v1/activities") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        ActivityDef d;
        d.id = static_cast<int>(JInt(body, "id"));
        d.type = JStr(body, "type");
        d.title = JStr(body, "title");
        if (body.contains("rules_json")) {
          if (body["rules_json"].is_object() || body["rules_json"].is_array())
            d.rules_json = body["rules_json"].dump();
          else
            d.rules_json = JStr(body, "rules_json", "{}");
        } else
          d.rules_json = "{}";
        d.enabled = JBool(body, "enabled", true);
        std::string err;
        if (!activity.UpsertDef(d, &err)) setJson(400, ErrObj(Err::kBadParam, trace, err));
        else {
          admin.Audit(s->admin_id, "upsert_activity", d.title, "", d.type);
          setJson(200, OkObj({{"ok", true}}, trace));
        }
      }
    } else if (method == "POST" && path == "/admin/v1/activities/simulate_settle") {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        const int64_t uid = JInt(body, "uid");
        const int tid = static_cast<int>(JInt(body, "template_id", 1));
        if (uid <= 0) setJson(400, ErrObj(Err::kBadParam, trace, "uid required"));
        else {
          try {
            activity.OnGameSettled(uid, tid);
          } catch (...) {
          }
          admin.Audit(s->admin_id, "simulate_settle", std::to_string(uid), "", std::to_string(tid));
          setJson(200, OkObj({{"ok", true}}, trace));
        }
      }
    } else if (method == "GET" && path.rfind("/admin/v1/reports/", 0) == 0) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        const std::string kind = path.substr(std::string("/admin/v1/reports/").size());
        int limit = 5000;
        auto lp = query.find("limit=");
        if (lp != std::string::npos) {
          try {
            limit = std::stoi(query.substr(lp + 6));
          } catch (...) {
          }
        }
        if (limit < 1) limit = 1;
        if (limit > 50000) limit = 50000;
        std::string csv;
        std::string fname;
        if (kind == "ledgers.csv") {
          csv = admin.ExportLedgersCsv(limit);
          fname = "ledgers.csv";
        } else if (kind == "rounds.csv") {
          csv = admin.ExportRoundsCsv(limit);
          fname = "rounds.csv";
        } else if (kind == "claims.csv") {
          csv = admin.ExportClaimsCsv(limit);
          fname = "claims.csv";
        } else {
          setJson(404, ErrObj(Err::kNotFound, trace));
          return out;
        }
        admin.Audit(s->admin_id, "export_report", fname, "", std::to_string(limit));
        out.status = 200;
        out.content_type = "text/csv; charset=utf-8";
        out.content_disposition = "attachment; filename=\"" + fname + "\"";
        out.body = std::move(csv);
      }
    } else if (method == "DELETE" && path.rfind("/admin/v1/activities/", 0) == 0) {
      auto s = requireAdmin(AdminRole::kOps);
      if (!s) setJson(403, ErrObj(Err::kForbidden, trace));
      else {
        const int id = static_cast<int>(PathUid(path, "/admin/v1/activities/"));
        std::string err;
        if (!activity.SetEnabled(id, false, &err)) setJson(400, ErrObj(Err::kBadParam, trace, err));
        else {
          admin.Audit(s->admin_id, "disable_activity", std::to_string(id), "", "0");
          setJson(200, OkObj({{"ok", true}}, trace));
        }
      }
    } else {
      setJson(404, ErrObj(Err::kNotFound, trace));
    }
  } else {
    setJson(404, ErrObj(Err::kNotFound, trace));
  }
  return out;
}

HttpResult HandleRequest(HttpApi* api, const http::request<http::string_body>& req) {
  return Dispatch(req, api->store(), api->auth(), api->wallet(), api->pay(), api->admin(), api->activity(),
                  api->mysql(), api->redis(), api->hub(), api->cfg());
}

std::shared_ptr<http::response<http::string_body>> MakeResponse(const http::request<http::string_body>& req,
                                                                HttpResult result) {
  auto res = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(result.status),
                                                                req.version());
  res->set(http::field::server, "pandora");
  res->set(http::field::content_type, result.content_type);
  res->set(http::field::access_control_allow_origin, "*");
  res->set(http::field::access_control_allow_headers, "Content-Type, Authorization");
  res->set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
  if (!result.content_disposition.empty()) res->set(http::field::content_disposition, result.content_disposition);
  res->keep_alive(false);
  res->body() = std::move(result.body);
  res->prepare_payload();
  return res;
}

class PlainHttpSession : public std::enable_shared_from_this<PlainHttpSession> {
 public:
  PlainHttpSession(tcp::socket socket, HttpApi* api) : stream_(std::move(socket)), api_(api) {}

  void Run() {
    http::async_read(stream_, buffer_, req_,
                     beast::bind_front_handler(&PlainHttpSession::OnRead, shared_from_this()));
  }

 private:
  void OnRead(beast::error_code ec, std::size_t) {
    if (ec) return;
    if (api_->cfg().net.force_tls) {
      stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
      return;
    }
    auto res = MakeResponse(req_, HandleRequest(api_, req_));
    auto self = shared_from_this();
    http::async_write(stream_, *res, [self, res](beast::error_code, std::size_t) {
      beast::error_code ignored;
      self->stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
    });
  }

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  HttpApi* api_;
};

class SslHttpSession : public std::enable_shared_from_this<SslHttpSession> {
 public:
  SslHttpSession(tcp::socket socket, ssl::context& ctx, HttpApi* api)
      : stream_(std::move(socket), ctx), api_(api) {}

  void Run() {
    stream_.async_handshake(ssl::stream_base::server,
                            beast::bind_front_handler(&SslHttpSession::OnHandshake, shared_from_this()));
  }

 private:
  void OnHandshake(beast::error_code ec) {
    if (ec) {
      PLOG_WARN("https handshake failed: " << ec.message());
      return;
    }
    http::async_read(stream_, buffer_, req_,
                     beast::bind_front_handler(&SslHttpSession::OnRead, shared_from_this()));
  }

  void OnRead(beast::error_code ec, std::size_t) {
    if (ec) return;
    auto res = MakeResponse(req_, HandleRequest(api_, req_));
    auto self = shared_from_this();
    http::async_write(stream_, *res, [self, res](beast::error_code, std::size_t) {
      beast::error_code ignored;
      self->stream_.shutdown(ignored);
    });
  }

  ssl::stream<tcp::socket> stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  HttpApi* api_;
};

class HttpListener : public std::enable_shared_from_this<HttpListener> {
 public:
  HttpListener(net::io_context& ioc, tcp::endpoint ep, HttpApi* api, std::shared_ptr<ssl::context> tls)
      : ioc_(ioc), acceptor_(ioc), api_(api), tls_(std::move(tls)) {
    beast::error_code ec;
    acceptor_.open(ep.protocol(), ec);
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    acceptor_.bind(ep, ec);
    if (ec) {
      PLOG_ERROR("http bind failed: " << ec.message());
      return;
    }
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
  }

  void Run() { DoAccept(); }

 private:
  void DoAccept() {
    acceptor_.async_accept(net::make_strand(ioc_), [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
      if (ec) return;
      if (self->tls_) {
        std::make_shared<SslHttpSession>(std::move(socket), *self->tls_, self->api_)->Run();
      } else {
        std::make_shared<PlainHttpSession>(std::move(socket), self->api_)->Run();
      }
      self->DoAccept();
    });
  }

  net::io_context& ioc_;
  tcp::acceptor acceptor_;
  HttpApi* api_;
  std::shared_ptr<ssl::context> tls_;
};

}  // namespace

HttpApi::HttpApi(AppConfig cfg, MemoryStore& store, AuthService& auth, WalletService& wallet, PayService& pay,
                 AdminService& admin, ActivityService& activity, MysqlClient& mysql, RedisClient& redis,
                 SessionHub& hub)
    : cfg_(std::move(cfg)),
      store_(store),
      auth_(auth),
      wallet_(wallet),
      pay_(pay),
      admin_(admin),
      activity_(activity),
      mysql_(mysql),
      redis_(redis),
      hub_(hub) {}

void HttpApi::Start(boost::asio::io_context& ioc, const std::filesystem::path& conf_dir) {
  std::shared_ptr<ssl::context> tls;
  try {
    tls = MakeTlsContext(cfg_.net, conf_dir);
  } catch (const std::exception& ex) {
    PLOG_ERROR("TLS init failed: " << ex.what());
    throw;
  }
  if (cfg_.net.force_tls && !tls) {
    PLOG_ERROR("force_tls=true but tls.enabled=false; refusing plaintext HTTP");
    throw std::runtime_error("force_tls requires tls.enabled");
  }
  auto const address = net::ip::make_address(cfg_.net.http_host == "0.0.0.0" ? "0.0.0.0" : cfg_.net.http_host);
  auto listener =
      std::make_shared<HttpListener>(ioc, tcp::endpoint{address, static_cast<unsigned short>(cfg_.net.http_port)}, this, tls);
  listener->Run();
  PLOG_INFO((tls ? "HTTPS" : "HTTP") << " Beast listening on :" << cfg_.net.http_port
                                     << " workers=" << cfg_.net.iocp_workers);
}

}  // namespace pandora
