#include "common/proto_wire.hpp"

#include <activity.pb.h>
#include <bag.pb.h>
#include <common.pb.h>
#include <game_ddz.pb.h>
#include <game_hzmj.pb.h>
#include <game_biji.pb.h>
#include <game_fish.pb.h>
#include <game_phz.pb.h>
#include <lobby.pb.h>
#include <social.pb.h>

namespace pandora {
namespace proto_wire {

namespace {

template <typename Msg>
std::vector<uint8_t> Serialize(const Msg& msg) {
  std::vector<uint8_t> out(msg.ByteSizeLong());
  msg.SerializeToArray(out.data(), static_cast<int>(out.size()));
  return out;
}

template <typename Msg>
bool Parse(const uint8_t* data, size_t len, Msg& msg) {
  return msg.ParseFromArray(data, static_cast<int>(len));
}

}  // namespace

std::vector<uint8_t> EncodeC2S_Auth(const std::string& token) {
  C2S_Auth m;
  m.set_token(token);
  return Serialize(m);
}

bool DecodeC2S_Auth(const uint8_t* data, size_t len, std::string& token) {
  C2S_Auth m;
  if (!Parse(data, len, m)) return false;
  token = m.token();
  return true;
}

std::vector<uint8_t> EncodeS2C_AuthResult(int32_t code, const std::string& message, int64_t uid) {
  S2C_AuthResult m;
  m.set_code(code);
  m.set_message(message);
  m.set_uid(uid);
  return Serialize(m);
}

std::vector<uint8_t> EncodeC2S_Heartbeat(int64_t client_time_ms) {
  C2S_Heartbeat m;
  m.set_client_time_ms(client_time_ms);
  return Serialize(m);
}

bool DecodeC2S_Heartbeat(const uint8_t* data, size_t len, int64_t& client_time_ms) {
  C2S_Heartbeat m;
  if (!Parse(data, len, m)) return false;
  client_time_ms = m.client_time_ms();
  return true;
}

std::vector<uint8_t> EncodeS2C_HeartbeatAck(int64_t server_time_ms) {
  S2C_HeartbeatAck m;
  m.set_server_time_ms(server_time_ms);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_Kick(int32_t reason, const std::string& message) {
  S2C_Kick m;
  m.set_reason(reason);
  m.set_message(message);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_Error(int32_t code, const std::string& message, uint32_t ref_msg_id) {
  S2C_Error m;
  m.set_code(code);
  m.set_message(message);
  m.set_ref_msg_id(ref_msg_id);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_LobbyInfo(const std::vector<LobbyTemplate>& templates, int64_t gold,
                                         int64_t diamond) {
  S2C_LobbyInfo m;
  for (const auto& t : templates) {
    auto* nt = m.add_templates();
    nt->set_id(t.id);
    nt->set_name(t.name);
    nt->set_base_score(t.base_score);
    nt->set_min_gold(t.min_gold);
    nt->set_max_gold(t.max_gold);
    nt->set_enabled(t.enabled);
    nt->set_game_id(t.game_id);
    nt->set_players(t.players);
  }
  m.set_gold(gold);
  m.set_diamond(diamond);
  return Serialize(m);
}

bool DecodeC2S_QuickMatch(const uint8_t* data, size_t len, int32_t& template_id) {
  C2S_QuickMatch m;
  if (!Parse(data, len, m)) return false;
  template_id = m.template_id();
  return true;
}

std::vector<uint8_t> EncodeS2C_MatchStatus(int32_t status, int64_t room_id, const std::string& message) {
  S2C_MatchStatus m;
  m.set_status(static_cast<S2C_MatchStatus_Status>(status));
  m.set_room_id(room_id);
  m.set_message(message);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_RoomState(int64_t room_id, int32_t template_id, const std::vector<RoomSeat>& seats,
                                         const std::string& phase) {
  S2C_RoomState m;
  m.set_room_id(room_id);
  m.set_template_id(template_id);
  m.set_phase(phase);
  for (const auto& s : seats) {
    auto* ns = m.add_seats();
    ns->set_seat_id(s.seat_id);
    ns->set_uid(s.uid);
    ns->set_nickname(s.nickname);
    ns->set_ready(s.ready);
    ns->set_online(s.online);
  }
  return Serialize(m);
}

bool DecodeC2S_Ready(const uint8_t* data, size_t len, bool& ready) {
  C2S_Ready m;
  if (!Parse(data, len, m)) return false;
  ready = m.ready();
  return true;
}

bool DecodeC2S_DdzBid(const uint8_t* data, size_t len, int32_t& score) {
  C2S_DdzBid m;
  if (!Parse(data, len, m)) return false;
  score = m.score();
  return true;
}

bool DecodeC2S_DdzPlay(const uint8_t* data, size_t len, bool& pass, std::vector<int32_t>& cards) {
  C2S_DdzPlay m;
  if (!Parse(data, len, m)) return false;
  pass = m.pass();
  cards.assign(m.cards().begin(), m.cards().end());
  return true;
}

std::vector<uint8_t> EncodeS2C_DdzGameStart(int32_t seat_id, const std::vector<int32_t>& hand, int32_t landlord_seat,
                                            const std::vector<int32_t>& bottom, int64_t round_id) {
  S2C_DdzGameStart m;
  m.set_seat_id(seat_id);
  for (auto c : hand) m.add_hand_cards(c);
  m.set_landlord_seat(landlord_seat);
  for (auto c : bottom) m.add_bottom_cards(c);
  m.set_round_id(round_id);
  return Serialize(m);
}

bool DecodeC2S_ClientTrace(const uint8_t* data, size_t len, int64_t& round_id, int32_t& seat_id, std::string& event,
                           std::string& detail, std::string& game) {
  C2S_ClientTrace m;
  if (!Parse(data, len, m)) return false;
  round_id = m.round_id();
  seat_id = m.seat_id();
  event = m.event();
  detail = m.detail();
  game = m.game();
  return true;
}

std::vector<uint8_t> EncodeS2C_DdzTurn(int32_t seat_id, const std::string& phase, int32_t timeout_s) {
  S2C_DdzTurn m;
  m.set_seat_id(seat_id);
  m.set_phase(phase);
  m.set_timeout_s(timeout_s);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_DdzBidBroadcast(int32_t seat_id, int32_t score) {
  S2C_DdzBidBroadcast m;
  m.set_seat_id(seat_id);
  m.set_score(score);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_DdzPlayBroadcast(int32_t seat_id, bool pass, const std::vector<int32_t>& cards,
                                                int32_t cards_left) {
  S2C_DdzPlayBroadcast m;
  m.set_seat_id(seat_id);
  m.set_pass(pass);
  for (auto c : cards) m.add_cards(c);
  m.set_cards_left(cards_left);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_DdzSettle(int64_t round_id, int32_t base_score, int32_t multiplier,
                                         const std::vector<SettleEntry>& entries) {
  S2C_DdzSettle m;
  m.set_round_id(round_id);
  m.set_base_score(base_score);
  m.set_multiplier(multiplier);
  for (const auto& e : entries) {
    auto* ne = m.add_entries();
    ne->set_uid(e.uid);
    ne->set_seat_id(e.seat_id);
    ne->set_delta_gold(e.delta_gold);
  }
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_DdzReconnect(int32_t seat_id, const std::string& phase,
                                            const std::vector<int32_t>& hand, int32_t landlord_seat,
                                            int32_t current_seat, int32_t timeout_s) {
  S2C_DdzReconnect m;
  m.set_seat_id(seat_id);
  m.set_phase(phase);
  for (auto c : hand) m.add_hand_cards(c);
  m.set_landlord_seat(landlord_seat);
  m.set_current_seat(current_seat);
  m.set_timeout_s(timeout_s);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_ActivityUpdate(int32_t activity_id, const std::string& type,
                                              const std::string& progress_json, bool claimable) {
  S2C_ActivityUpdate m;
  m.set_activity_id(activity_id);
  m.set_type(type);
  m.set_progress_json(progress_json);
  m.set_claimable(claimable);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_MailNotify(int64_t mail_id, const std::string& title, bool has_attach) {
  S2C_MailNotify m;
  m.set_mail_id(mail_id);
  m.set_title(title);
  m.set_has_attach(has_attach);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_FriendNotify(int32_t kind, int64_t from_uid, const std::string& nickname) {
  S2C_FriendNotify m;
  m.set_kind(kind);
  m.set_from_uid(from_uid);
  m.set_nickname(nickname);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_BagUpdate(int32_t item_id, int64_t quantity, const std::string& expire_at,
                                          int32_t reason) {
  S2C_BagUpdate m;
  m.set_item_id(item_id);
  m.set_quantity(quantity);
  m.set_expire_at(expire_at);
  m.set_reason(reason);
  return Serialize(m);
}

bool DecodeC2S_HzmjDiscard(const uint8_t* data, size_t len, int32_t& tile) {
  C2S_HzmjDiscard m;
  if (!Parse(data, len, m)) return false;
  tile = m.tile();
  return true;
}

bool DecodeC2S_HzmjAction(const uint8_t* data, size_t len, int32_t& action, std::vector<int32_t>& chi_hand) {
  C2S_HzmjAction m;
  if (!Parse(data, len, m)) return false;
  action = m.action();
  chi_hand.assign(m.chi_hand_tiles().begin(), m.chi_hand_tiles().end());
  return true;
}

bool DecodeC2S_HzmjGang(const uint8_t* data, size_t len, int32_t& kind, int32_t& tile) {
  C2S_HzmjGang m;
  if (!Parse(data, len, m)) return false;
  kind = m.kind();
  tile = m.tile();
  return true;
}

std::vector<uint8_t> EncodeS2C_HzmjGameStart(int64_t round_id, int64_t room_id, int32_t template_id, int32_t banker_seat,
                                             int32_t lian_zhuang, int32_t N, const std::vector<int32_t>& caishen,
                                             const std::vector<int32_t>& self_hand, int32_t wall_remain,
                                             int32_t self_seat, int32_t base_score) {
  S2C_HzmjGameStart m;
  m.set_round_id(round_id);
  m.set_room_id(room_id);
  m.set_template_id(template_id);
  m.set_banker_seat(banker_seat);
  m.set_lian_zhuang(lian_zhuang);
  m.set_n(N);
  for (auto t : caishen) m.add_caishen_tiles(t);
  for (auto t : self_hand) m.add_self_hand(t);
  m.set_wall_remain(wall_remain);
  m.set_self_seat(self_seat);
  m.set_base_score(base_score);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_HzmjTurn(int32_t seat_id, const std::string& sub, int32_t timeout_s, int32_t wall_remain,
                                        int32_t piao_seat, const std::vector<int32_t>& self_hand, bool can_zimo) {
  S2C_HzmjTurn m;
  m.set_seat_id(seat_id);
  m.set_sub(sub);
  m.set_timeout_s(timeout_s);
  m.set_wall_remain(wall_remain);
  m.set_piao_seat(piao_seat);
  for (auto t : self_hand) m.add_self_hand(t);
  m.set_can_zimo(can_zimo);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_HzmjDraw(int32_t seat_id, int32_t tile) {
  S2C_HzmjDraw m;
  m.set_seat_id(seat_id);
  m.set_tile(tile);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_HzmjDiscardBroadcast(int32_t seat_id, int32_t tile) {
  S2C_HzmjDiscardBroadcast m;
  m.set_seat_id(seat_id);
  m.set_tile(tile);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_HzmjActionBroadcast(int32_t seat_id, int32_t action, int32_t tile,
                                                   const std::vector<int32_t>& tiles, int32_t from_seat,
                                                   int32_t meld_kind) {
  S2C_HzmjActionBroadcast m;
  m.set_seat_id(seat_id);
  m.set_action(action);
  m.set_tile(tile);
  for (auto t : tiles) m.add_tiles(t);
  if (from_seat >= 0) m.set_from_seat(from_seat);
  if (meld_kind != 0) m.set_meld_kind(meld_kind);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_HzmjSettle(int64_t round_id, int32_t winner_seat, int32_t hu_tile, bool is_zimo,
                                          int32_t shooter_seat, int32_t M, int32_t N, int32_t contractor_seat,
                                          int32_t base_score, const std::vector<SettleEntry>& entries) {
  S2C_HzmjSettle m;
  m.set_round_id(round_id);
  m.set_winner_seat(winner_seat);
  m.set_hu_tile(hu_tile);
  m.set_is_zimo(is_zimo);
  m.set_shooter_seat(shooter_seat);
  m.set_m(M);
  m.set_n(N);
  m.set_contractor_seat(contractor_seat);
  m.set_base_score(base_score);
  for (const auto& e : entries) {
    auto* ne = m.add_entries();
    ne->set_uid(e.uid);
    ne->set_seat_id(e.seat_id);
    ne->set_delta_gold(e.delta_gold);
  }
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_HzmjLiuJu(int32_t lian_zhuang) {
  S2C_HzmjLiuJu m;
  m.set_lian_zhuang(lian_zhuang);
  return Serialize(m);
}

bool DecodeC2S_PhzDiscard(const uint8_t* data, size_t len, int32_t& tile) {
  C2S_PhzDiscard m;
  if (!Parse(data, len, m)) return false;
  tile = m.tile();
  return true;
}

bool DecodeC2S_PhzAction(const uint8_t* data, size_t len, int32_t& action, std::vector<int32_t>& chi_hand) {
  C2S_PhzAction m;
  if (!Parse(data, len, m)) return false;
  action = m.action();
  chi_hand.clear();
  for (auto t : m.chi_hand_tiles()) chi_hand.push_back(t);
  return true;
}

std::vector<uint8_t> EncodeS2C_PhzGameStart(int64_t round_id, int64_t room_id, int32_t template_id, int32_t banker_seat,
                                            const std::vector<int32_t>& self_hand, int32_t wall_remain, int32_t self_seat,
                                            int32_t base_score, const std::string& cfg_snapshot) {
  S2C_PhzGameStart m;
  m.set_round_id(round_id);
  m.set_room_id(room_id);
  m.set_template_id(template_id);
  m.set_banker_seat(banker_seat);
  for (auto t : self_hand) m.add_self_hand(t);
  m.set_wall_remain(wall_remain);
  m.set_self_seat(self_seat);
  m.set_base_score(base_score);
  m.set_cfg_snapshot(cfg_snapshot);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_PhzTurn(int32_t seat_id, const std::string& sub, int32_t timeout_s, int32_t wall_remain,
                                       const std::vector<int32_t>& self_hand, bool can_hu) {
  S2C_PhzTurn m;
  m.set_seat_id(seat_id);
  m.set_sub(sub);
  m.set_timeout_s(timeout_s);
  m.set_wall_remain(wall_remain);
  for (auto t : self_hand) m.add_self_hand(t);
  m.set_can_hu(can_hu);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_PhzDraw(int32_t seat_id, int32_t tile) {
  S2C_PhzDraw m;
  m.set_seat_id(seat_id);
  m.set_tile(tile);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_PhzReveal(int32_t seat_id, int32_t tile) {
  S2C_PhzReveal m;
  m.set_seat_id(seat_id);
  m.set_tile(tile);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_PhzDiscardBroadcast(int32_t seat_id, int32_t tile) {
  S2C_PhzDiscardBroadcast m;
  m.set_seat_id(seat_id);
  m.set_tile(tile);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_PhzActionBroadcast(int32_t seat_id, int32_t action, int32_t tile,
                                                  const std::vector<int32_t>& tiles, int32_t from_seat,
                                                  int32_t meld_kind) {
  S2C_PhzActionBroadcast m;
  m.set_seat_id(seat_id);
  m.set_action(action);
  m.set_tile(tile);
  for (auto t : tiles) m.add_tiles(t);
  if (from_seat >= 0) m.set_from_seat(from_seat);
  if (meld_kind != 0) m.set_meld_kind(meld_kind);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_PhzSettle(int64_t round_id, int32_t winner_seat, int32_t hu_tile, bool is_draw_win,
                                         int32_t hu_xi, int32_t tun, int32_t fan, int32_t ming_tang_mask,
                                         int32_t base_score, const std::vector<SettleEntry>& entries) {
  S2C_PhzSettle m;
  m.set_round_id(round_id);
  m.set_winner_seat(winner_seat);
  m.set_hu_tile(hu_tile);
  m.set_is_draw_win(is_draw_win);
  m.set_hu_xi(hu_xi);
  m.set_tun(tun);
  m.set_fan(fan);
  m.set_ming_tang_mask(ming_tang_mask);
  m.set_base_score(base_score);
  for (const auto& e : entries) {
    auto* ne = m.add_entries();
    ne->set_uid(e.uid);
    ne->set_seat_id(e.seat_id);
    ne->set_delta_gold(e.delta_gold);
  }
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_PhzLiuJu(int32_t banker_seat) {
  S2C_PhzLiuJu m;
  m.set_banker_seat(banker_seat);
  return Serialize(m);
}

namespace {

void FillFishSeat(FishSeatInfo* out, const FishSeatData& s) {
  out->set_seat_id(s.seat_id);
  out->set_uid(s.uid);
  out->set_nickname(s.nickname);
  out->set_cannon_mult(s.cannon_mult);
  out->set_online(s.online);
  out->set_gold(s.gold);
  out->set_last_client_seq(s.last_client_seq);
}

void FillFishSnap(FishSnapshot* out, const FishSnapData& f) {
  out->set_fish_id(f.fish_id);
  out->set_type_id(f.type_id);
  out->set_x(f.x);
  out->set_y(f.y);
  out->set_vx(f.vx);
  out->set_vy(f.vy);
  out->set_radius(f.radius);
  out->set_hp(f.hp);
  out->set_hp_max(f.hp_max);
}

}  // namespace

bool DecodeC2S_FishFire(const uint8_t* data, size_t len, int32_t& mult, float& aim_x, float& aim_y,
                        int64_t& lock_fish_id, int64_t& client_seq) {
  C2S_FishFire m;
  if (!Parse(data, len, m)) return false;
  mult = m.mult();
  aim_x = m.aim_x();
  aim_y = m.aim_y();
  lock_fish_id = m.lock_fish_id();
  client_seq = m.client_seq();
  return true;
}

bool DecodeC2S_FishSetMult(const uint8_t* data, size_t len, int32_t& mult) {
  C2S_FishSetMult m;
  if (!Parse(data, len, m)) return false;
  mult = m.mult();
  return true;
}

std::vector<uint8_t> EncodeS2C_FishGameStart(const FishGameStartData& data) {
  S2C_FishGameStart m;
  m.set_round_id(data.round_id);
  m.set_room_id(data.room_id);
  m.set_template_id(data.template_id);
  m.set_self_seat(data.self_seat);
  m.set_base_score(data.base_score);
  for (auto v : data.cannon_mults) m.add_cannon_mults(v);
  for (const auto& s : data.seats) FillFishSeat(m.add_seats(), s);
  for (const auto& f : data.fish) FillFishSnap(m.add_fish(), f);
  m.set_cfg_snapshot(data.cfg_snapshot);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_FishSeatUpdate(const FishSeatData& seat) {
  S2C_FishSeatUpdate m;
  FillFishSeat(m.mutable_seat(), seat);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_FishSpawn(const std::vector<FishSnapData>& fish) {
  S2C_FishSpawn m;
  for (const auto& f : fish) FillFishSnap(m.add_fish(), f);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_FishDespawn(const std::vector<int64_t>& fish_ids, const std::string& reason) {
  S2C_FishDespawn m;
  for (auto id : fish_ids) m.add_fish_ids(id);
  m.set_reason(reason);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_FishFireBroadcast(const FishFireBroadcastData& data) {
  S2C_FishFireBroadcast m;
  m.set_seat_id(data.seat_id);
  m.set_uid(data.uid);
  m.set_bullet_id(data.bullet_id);
  m.set_mult(data.mult);
  m.set_x(data.x);
  m.set_y(data.y);
  m.set_vx(data.vx);
  m.set_vy(data.vy);
  m.set_client_seq(data.client_seq);
  m.set_gold(data.gold);
  m.set_cost(data.cost);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_FishHit(int64_t bullet_id, int64_t fish_id, int32_t seat_id, int32_t hp, int32_t hp_max) {
  S2C_FishHit m;
  m.set_bullet_id(bullet_id);
  m.set_fish_id(fish_id);
  m.set_seat_id(seat_id);
  m.set_hp(hp);
  m.set_hp_max(hp_max);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_FishCatch(int64_t fish_id, int32_t type_id, int32_t seat_id, int64_t uid, int64_t reward,
                                         int64_t gold) {
  S2C_FishCatch m;
  m.set_fish_id(fish_id);
  m.set_type_id(type_id);
  m.set_seat_id(seat_id);
  m.set_uid(uid);
  m.set_reward(reward);
  m.set_gold(gold);
  return Serialize(m);
}

bool DecodeC2S_BijiArrange(const uint8_t* data, size_t len, std::vector<int32_t>& head, std::vector<int32_t>& mid,
                           std::vector<int32_t>& tail, bool& confirm) {
  C2S_BijiArrange m;
  if (!Parse(data, len, m)) return false;
  head.assign(m.head().begin(), m.head().end());
  mid.assign(m.mid().begin(), m.mid().end());
  tail.assign(m.tail().begin(), m.tail().end());
  confirm = m.confirm();
  return true;
}

std::vector<uint8_t> EncodeS2C_BijiGameStart(const BijiGameStartData& data) {
  S2C_BijiGameStart m;
  m.set_round_id(data.round_id);
  m.set_room_id(data.room_id);
  m.set_template_id(data.template_id);
  m.set_self_seat(data.self_seat);
  m.set_players(data.players);
  m.set_deal_start(data.deal_start);
  m.set_base_score(data.base_score);
  m.set_arrange_timeout_s(data.arrange_timeout_s);
  for (auto c : data.hand) m.add_hand(c);
  m.set_enable_chixi(data.enable_chixi);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_BijiArrangeState(const BijiArrangeStateData& data) {
  S2C_BijiArrangeState m;
  for (bool v : data.locked) m.add_locked(v);
  for (bool v : data.trusteeship) m.add_trusteeship(v);
  m.set_remain_s(data.remain_s);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_BijiArrangeAck(int32_t code, const std::string& message, bool locked) {
  S2C_BijiArrangeAck m;
  m.set_code(code);
  m.set_message(message);
  m.set_locked(locked);
  return Serialize(m);
}

namespace {

void FillDun(::pandora::BijiDun* d, const BijiDunData& src) {
  for (auto c : src.cards) d->add_cards(c);
  d->set_type(src.type);
  d->set_place(src.place);
  d->set_delta(src.delta);
}

void FillCompareSeat(::pandora::BijiSeatCompare* s, const BijiSeatCompareData& src) {
  s->set_seat_id(src.seat_id);
  s->set_uid(src.uid);
  FillDun(s->mutable_head(), src.head);
  FillDun(s->mutable_mid(), src.mid);
  FillDun(s->mutable_tail(), src.tail);
}

void FillSettleSeat(::pandora::BijiSeatSettle* s, const BijiSeatSettleData& src) {
  s->set_seat_id(src.seat_id);
  s->set_uid(src.uid);
  s->set_dun_delta_head(src.dun_delta_head);
  s->set_dun_delta_mid(src.dun_delta_mid);
  s->set_dun_delta_tail(src.dun_delta_tail);
  s->set_chixi_delta(src.chixi_delta);
  s->set_gross(src.gross);
  s->set_rake(src.rake);
  s->set_net(src.net);
  s->set_gold(src.gold);
  for (const auto& c : src.chixi) {
    auto* x = s->add_chixi();
    x->set_type(c.type);
    x->set_mult(c.mult);
  }
}

}  // namespace

std::vector<uint8_t> EncodeS2C_BijiCompare(const BijiCompareData& data) {
  S2C_BijiCompare m;
  m.set_round_id(data.round_id);
  for (const auto& s : data.seats) FillCompareSeat(m.add_seats(), s);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_BijiSettle(const BijiSettleData& data) {
  S2C_BijiSettle m;
  m.set_round_id(data.round_id);
  for (const auto& s : data.seats) FillSettleSeat(m.add_seats(), s);
  return Serialize(m);
}

std::vector<uint8_t> EncodeS2C_BijiSnapshot(const BijiSnapshotData& data) {
  S2C_BijiSnapshot m;
  m.set_phase(data.phase);
  m.set_deal_start(data.deal_start);
  m.set_remain_s(data.remain_s);
  for (auto c : data.hand) m.add_hand(c);
  for (auto c : data.draft_head) m.add_draft_head(c);
  for (auto c : data.draft_mid) m.add_draft_mid(c);
  for (auto c : data.draft_tail) m.add_draft_tail(c);
  m.set_has_draft(data.has_draft);
  m.set_locked(data.locked);
  for (bool v : data.others_locked) m.add_others_locked(v);
  return Serialize(m);
}

}  // namespace proto_wire
}  // namespace pandora