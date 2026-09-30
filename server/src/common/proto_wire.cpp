#include "common/proto_wire.hpp"

#include <activity.pb.h>
#include <bag.pb.h>
#include <common.pb.h>
#include <game_ddz.pb.h>
#include <game_hzmj.pb.h>
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

}  // namespace proto_wire
}  // namespace pandora