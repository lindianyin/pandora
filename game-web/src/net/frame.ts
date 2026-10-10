/**
 * Wire framing + thin wrappers over ts-proto generated messages (src/gen).
 * Regenerate: powershell -File scripts/gen-proto.ps1
 */
import {
  C2S_Auth,
  C2S_ClientTrace,
  C2S_Heartbeat,
  S2C_AuthResult,
  S2C_Error,
  S2C_Kick,
} from '../gen/common'
import {
  C2S_CancelMatch,
  C2S_GetLobby,
  C2S_LeaveRoom,
  C2S_QuickMatch,
  C2S_Ready,
  S2C_LobbyInfo,
  S2C_MatchStatus,
  S2C_RoomState,
} from '../gen/lobby'
import {
  C2S_DdzBid,
  C2S_DdzPlay,
  S2C_DdzBidBroadcast,
  S2C_DdzGameStart,
  S2C_DdzPlayBroadcast,
  S2C_DdzReconnect,
  S2C_DdzSettle,
  S2C_DdzTurn,
} from '../gen/game_ddz'
import {
  C2S_HzmjAction,
  C2S_HzmjDiscard,
  C2S_HzmjGang,
  S2C_HzmjActionBroadcast,
  S2C_HzmjDiscardBroadcast,
  S2C_HzmjDraw,
  S2C_HzmjGameStart,
  S2C_HzmjLiuJu,
  S2C_HzmjSettle,
  S2C_HzmjTurn,
} from '../gen/game_hzmj'
import {
  C2S_PhzAction,
  C2S_PhzDiscard,
  S2C_PhzActionBroadcast,
  S2C_PhzDiscardBroadcast,
  S2C_PhzDraw,
  S2C_PhzGameStart,
  S2C_PhzLiuJu,
  S2C_PhzReveal,
  S2C_PhzSettle,
  S2C_PhzTurn,
} from '../gen/game_phz'
import {
  C2S_FishFire,
  C2S_FishLeave,
  C2S_FishSetMult,
  S2C_FishCatch,
  S2C_FishDespawn,
  S2C_FishFireBroadcast,
  S2C_FishGameStart,
  S2C_FishHit,
  S2C_FishSeatUpdate,
  S2C_FishSpawn,
} from '../gen/game_fish'
import {
  C2S_BijiArrange,
  S2C_BijiArrangeAck,
  S2C_BijiArrangeState,
  S2C_BijiCompare,
  S2C_BijiGameStart,
  S2C_BijiSettle,
  S2C_BijiSnapshot,
} from '../gen/game_biji'
import { S2C_ActivityUpdate } from '../gen/activity'

export const MsgId = {
  C2S_Auth: 1,
  S2C_AuthResult: 2,
  C2S_Heartbeat: 3,
  S2C_HeartbeatAck: 4,
  S2C_Kick: 5,
  S2C_Error: 7,
  C2S_GetLobby: 1001,
  S2C_LobbyInfo: 1002,
  C2S_QuickMatch: 1010,
  C2S_CancelMatch: 1011,
  S2C_MatchStatus: 1012,
  C2S_LeaveRoom: 1021,
  C2S_Ready: 1022,
  S2C_RoomState: 1023,
  S2C_DdzGameStart: 200001,
  S2C_DdzTurn: 200002,
  C2S_DdzBid: 200003,
  S2C_DdzBidBroadcast: 200004,
  C2S_DdzPlay: 200005,
  S2C_DdzPlayBroadcast: 200006,
  S2C_DdzSettle: 200007,
  S2C_DdzReconnect: 200008,
  S2C_ActivityUpdate: 3001,
  S2C_BagUpdate: 5001,
  S2C_HzmjGameStart: 300001,
  S2C_HzmjTurn: 300002,
  S2C_HzmjDraw: 300003,
  C2S_HzmjDiscard: 300005,
  S2C_HzmjDiscardBroadcast: 300006,
  C2S_HzmjAction: 300007,
  S2C_HzmjActionBroadcast: 300008,
  S2C_HzmjSettle: 300009,
  S2C_HzmjLiuJu: 300010,
  C2S_HzmjGang: 300011,
  S2C_PhzGameStart: 400001,
  S2C_PhzTurn: 400002,
  S2C_PhzDraw: 400003,
  S2C_PhzReveal: 400004,
  C2S_PhzDiscard: 400005,
  S2C_PhzDiscardBroadcast: 400006,
  C2S_PhzAction: 400007,
  S2C_PhzActionBroadcast: 400008,
  S2C_PhzSettle: 400009,
  S2C_PhzLiuJu: 400010,
  S2C_FishGameStart: 500001,
  S2C_FishSeatUpdate: 500002,
  S2C_FishSpawn: 500003,
  S2C_FishDespawn: 500004,
  S2C_FishSync: 500005,
  C2S_FishFire: 500006,
  S2C_FishFireBroadcast: 500007,
  S2C_FishHit: 500008,
  S2C_FishCatch: 500009,
  C2S_FishSetMult: 500010,
  C2S_FishLeave: 500011,
  S2C_FishKickSeat: 500012,
  S2C_FishWave: 500013,
  C2S_FishLock: 500014,
  S2C_BijiGameStart: 600001,
  S2C_BijiArrangeState: 600002,
  C2S_BijiArrange: 600003,
  S2C_BijiArrangeAck: 600004,
  S2C_BijiCompare: 600005,
  S2C_BijiSettle: 600006,
  S2C_BijiSnapshot: 600007,
  C2S_BijiReady: 600008,
  C2S_BijiLeave: 600009,
  C2S_ClientTrace: 9001,
} as const

/** S2C_Kick.reason */
export const KickReason = {
  HeartbeatTimeout: 1,
  LoggedInElsewhere: 2,
} as const

/** Four-digit game_id: msg_id = game_id * 100 + slot */
export const GameId = {
  Ddz: 2000,
  Hzmj: 3000,
  Phz: 4000,
  Fish: 5000,
  Biji: 6000,
} as const

function writeU32LE(buf: Uint8Array, offset: number, value: number) {
  buf[offset] = value & 0xff
  buf[offset + 1] = (value >>> 8) & 0xff
  buf[offset + 2] = (value >>> 16) & 0xff
  buf[offset + 3] = (value >>> 24) & 0xff
}

function readU32LE(buf: Uint8Array, offset: number): number {
  return (
    (buf[offset]! |
      (buf[offset + 1]! << 8) |
      (buf[offset + 2]! << 16) |
      (buf[offset + 3]! << 24)) >>>
    0
  )
}

export function encodeFrame(msgId: number, body: Uint8Array): ArrayBuffer {
  const length = 4 + body.length
  const out = new Uint8Array(8 + body.length)
  writeU32LE(out, 0, length)
  writeU32LE(out, 4, msgId)
  out.set(body, 8)
  return out.buffer
}

export type DecodedFrame = { msgId: number; body: Uint8Array }

export function tryDecodeFrames(buffer: Uint8Array): { frames: DecodedFrame[]; rest: Uint8Array } {
  const frames: DecodedFrame[] = []
  let offset = 0
  while (buffer.length - offset >= 8) {
    const length = readU32LE(buffer, offset)
    if (length < 4 || length > 1024 * 1024) {
      return { frames, rest: new Uint8Array(0) }
    }
    const total = 4 + length
    if (buffer.length - offset < total) break
    const msgId = readU32LE(buffer, offset + 4)
    const body = buffer.slice(offset + 8, offset + total)
    frames.push({ msgId, body })
    offset += total
  }
  return { frames, rest: buffer.slice(offset) }
}

function enc<T>(fn: { encode(m: T): { finish(): Uint8Array } }, msg: T): Uint8Array {
  return fn.encode(msg).finish()
}

export type LobbyTemplate = {
  id: number
  name: string
  base_score: number
  min_gold: number
  max_gold: number
  enabled: boolean
  game_id: number
  players: number
}

export type RoomSeat = {
  seat_id: number
  uid: number
  nickname: string
  ready: boolean
  online: boolean
  trusteeship: boolean
}

export type HzmjGameStart = {
  round_id: number
  room_id: number
  template_id: number
  banker_seat: number
  lian_zhuang: number
  N: number
  caishen: number[]
  self_hand: number[]
  wall_remain: number
  self_seat: number
  base_score: number
}

export type HzmjSettle = {
  round_id: number
  winner_seat: number
  hu_tile: number
  is_zimo: boolean
  shooter_seat: number
  M: number
  N: number
  contractor_seat: number
  base_score: number
  entries: { uid: number; seat_id: number; delta_gold: number }[]
}

export type PhzGameStart = {
  round_id: number
  room_id: number
  template_id: number
  banker_seat: number
  self_hand: number[]
  wall_remain: number
  self_seat: number
  base_score: number
  cfg_snapshot: string
}

export type PhzSettle = {
  round_id: number
  winner_seat: number
  hu_tile: number
  is_draw_win: boolean
  hu_xi: number
  tun: number
  fan: number
  ming_tang_mask: number
  base_score: number
  entries: { uid: number; seat_id: number; delta_gold: number }[]
}

export function encodeC2S_Auth(token: string): Uint8Array {
  return enc(C2S_Auth, { token })
}

export function encodeC2S_ClientTrace(
  roundId: number,
  seatId: number,
  event: string,
  detail: string,
  game: string,
): Uint8Array {
  return enc(C2S_ClientTrace, {
    round_id: roundId,
    seat_id: seatId,
    event: event.slice(0, 40),
    detail: detail.replace(/[\r\n\t]/g, ' ').slice(0, 500),
    game,
  })
}

export function encodeC2S_Heartbeat(clientTimeMs: number): Uint8Array {
  return enc(C2S_Heartbeat, { client_time_ms: clientTimeMs })
}

export function encodeC2S_GetLobby(): Uint8Array {
  return enc(C2S_GetLobby, {})
}

export function encodeC2S_QuickMatch(templateId: number): Uint8Array {
  return enc(C2S_QuickMatch, { template_id: templateId })
}

export function encodeC2S_CancelMatch(): Uint8Array {
  return enc(C2S_CancelMatch, {})
}

export function encodeC2S_Ready(ready: boolean): Uint8Array {
  return enc(C2S_Ready, { ready })
}

export function encodeC2S_LeaveRoom(): Uint8Array {
  return enc(C2S_LeaveRoom, {})
}

export function encodeC2S_DdzBid(score: number): Uint8Array {
  return enc(C2S_DdzBid, { score })
}

export function encodeC2S_DdzPlay(pass: boolean, cards: number[]): Uint8Array {
  return enc(C2S_DdzPlay, { pass, cards })
}

export function encodeC2S_HzmjDiscard(tile: number): Uint8Array {
  return enc(C2S_HzmjDiscard, { tile })
}

export function encodeC2S_HzmjAction(action: number, chiHand: number[] = []): Uint8Array {
  return enc(C2S_HzmjAction, { action, chi_hand_tiles: chiHand })
}

export function encodeC2S_HzmjGang(kind: number, tile: number): Uint8Array {
  return enc(C2S_HzmjGang, { kind, tile })
}

export function encodeC2S_PhzDiscard(tile: number): Uint8Array {
  return enc(C2S_PhzDiscard, { tile })
}

export function encodeC2S_PhzAction(action: number, chiHand: number[] = []): Uint8Array {
  return enc(C2S_PhzAction, { action, chi_hand_tiles: chiHand })
}

export function decodeS2C_Kick(body: Uint8Array): { reason: number; message: string } {
  const m = S2C_Kick.decode(body)
  return { reason: m.reason, message: m.message }
}

export function decodeS2C_AuthResult(body: Uint8Array): { code: number; message: string; uid: number } {
  const m = S2C_AuthResult.decode(body)
  return { code: m.code, message: m.message, uid: m.uid }
}

export function decodeS2C_LobbyInfo(body: Uint8Array): {
  templates: LobbyTemplate[]
  gold: number
  diamond: number
} {
  const m = S2C_LobbyInfo.decode(body)
  return {
    templates: m.templates.map((t) => ({
      id: t.id,
      name: t.name,
      base_score: t.base_score,
      min_gold: t.min_gold,
      max_gold: t.max_gold,
      enabled: t.enabled,
      game_id: t.game_id || GameId.Ddz,
      players:
        t.players ||
        (t.game_id === GameId.Fish || t.game_id === 4
          ? 1
          : t.game_id === GameId.Hzmj || t.game_id === 2
            ? 4
            : 3),
    })),
    gold: m.gold,
    diamond: m.diamond,
  }
}

export function decodeS2C_MatchStatus(body: Uint8Array): { status: number; room_id: number; message: string } {
  const m = S2C_MatchStatus.decode(body)
  return { status: Number(m.status), room_id: m.room_id, message: m.message }
}

export function decodeS2C_RoomState(body: Uint8Array): {
  room_id: number
  template_id: number
  seats: RoomSeat[]
  phase: string
} {
  const m = S2C_RoomState.decode(body)
  return {
    room_id: m.room_id,
    template_id: m.template_id,
    phase: m.phase,
    seats: m.seats.map((s) => ({
      seat_id: s.seat_id,
      uid: s.uid,
      nickname: s.nickname,
      ready: s.ready,
      online: s.online,
      trusteeship: false,
    })),
  }
}

export function decodeS2C_DdzGameStart(body: Uint8Array): {
  seat_id: number
  hand_cards: number[]
  landlord_seat: number
  bottom_cards: number[]
  round_id: number
} {
  const m = S2C_DdzGameStart.decode(body)
  return {
    seat_id: m.seat_id,
    hand_cards: [...m.hand_cards],
    landlord_seat: m.landlord_seat,
    bottom_cards: [...m.bottom_cards],
    round_id: m.round_id,
  }
}

export function decodeS2C_DdzTurn(body: Uint8Array): { seat_id: number; phase: string; timeout_s: number } {
  const m = S2C_DdzTurn.decode(body)
  return { seat_id: m.seat_id, phase: m.phase, timeout_s: m.timeout_s }
}

export function decodeS2C_DdzBidBroadcast(body: Uint8Array): { seat_id: number; score: number } {
  const m = S2C_DdzBidBroadcast.decode(body)
  return { seat_id: m.seat_id, score: m.score }
}

export function decodeS2C_DdzPlayBroadcast(body: Uint8Array): {
  seat_id: number
  pass: boolean
  cards: number[]
  cards_left: number
} {
  const m = S2C_DdzPlayBroadcast.decode(body)
  return { seat_id: m.seat_id, pass: m.pass, cards: [...m.cards], cards_left: m.cards_left }
}

export function decodeS2C_DdzSettle(body: Uint8Array): {
  round_id: number
  base_score: number
  multiplier: number
  entries: { uid: number; seat_id: number; delta_gold: number }[]
} {
  const m = S2C_DdzSettle.decode(body)
  return {
    round_id: m.round_id,
    base_score: m.base_score,
    multiplier: m.multiplier,
    entries: m.entries.map((e) => ({ uid: e.uid, seat_id: e.seat_id, delta_gold: e.delta_gold })),
  }
}

export function decodeS2C_DdzReconnect(body: Uint8Array): {
  seat_id: number
  phase: string
  hand: number[]
  landlord_seat: number
  current_seat: number
  timeout_s: number
} {
  const m = S2C_DdzReconnect.decode(body)
  return {
    seat_id: m.seat_id,
    phase: m.phase,
    hand: [...m.hand_cards],
    landlord_seat: m.landlord_seat,
    current_seat: m.current_seat,
    timeout_s: m.timeout_s,
  }
}

export function decodeS2C_ActivityUpdate(body: Uint8Array): {
  activity_id: number
  type: string
  progress_json: string
  claimable: boolean
} {
  const m = S2C_ActivityUpdate.decode(body)
  return {
    activity_id: m.activity_id,
    type: m.type,
    progress_json: m.progress_json,
    claimable: m.claimable,
  }
}

export function decodeS2C_Error(body: Uint8Array): { code: number; message: string; ref_msg_id: number } {
  const m = S2C_Error.decode(body)
  return { code: m.code, message: m.message, ref_msg_id: m.ref_msg_id }
}

export function cardLabel(card: number): string {
  if (card === 52) return '小王'
  if (card === 53) return '大王'
  const ranks = ['3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K', 'A', '2']
  const suits = ['♠', '♥', '♣', '♦']
  return suits[Math.floor(card / 13) % 4]! + ranks[card % 13]!
}

export type DdzCardFace = {
  id: number
  rank: string
  suit: string
  red: boolean
  joker: boolean
  label: string
}

/** Poker-style face for DDZ UI (♠♥♣♦). */
export function ddzCardFace(card: number): DdzCardFace {
  if (card === 52) {
    return { id: card, rank: 'JOKER', suit: '🃏', red: false, joker: true, label: '小王' }
  }
  if (card === 53) {
    return { id: card, rank: 'JOKER', suit: '🃏', red: true, joker: true, label: '大王' }
  }
  const ranks = ['3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K', 'A', '2']
  const suits = ['♠', '♥', '♣', '♦']
  const suit = Math.floor(card / 13) % 4
  const rank = card % 13
  const red = suit === 1 || suit === 3
  return {
    id: card,
    rank: ranks[rank] ?? '?',
    suit: suits[suit] ?? '?',
    red,
    joker: false,
    label: cardLabel(card),
  }
}

/** DDZ card sort: by card id rank within suit encoding (c%13), jokers last */
export function sortDdzHand(cards: number[]): number[] {
  const rankKey = (c: number) => (c >= 52 ? 100 + (c - 52) : c % 13)
  const suitKey = (c: number) => (c >= 52 ? 0 : Math.floor(c / 13) % 4)
  return [...cards].sort((a, b) => {
    const dr = rankKey(a) - rankKey(b)
    if (dr !== 0) return dr
    return suitKey(a) - suitKey(b)
  })
}

/** Hangzhou mahjong TileId 0..33 */
export function tileLabel(tile: number): string {
  if (tile < 0 || tile > 33) return '?'
  if (tile <= 8) return `${tile + 1}万`
  if (tile <= 17) return `${tile - 8}条`
  if (tile <= 26) return `${tile - 17}筒`
  const winds = ['东', '南', '西', '北']
  if (tile <= 30) return winds[tile - 27]!
  const dragons = ['中', '发', '白']
  return dragons[tile - 31]!
}

export type HzmjTileFace = {
  id: number
  num: string
  kind: string
  suit: 'wan' | 'tiao' | 'tong' | 'zi'
  label: string
}

/** Split label for mahjong tile UI. */
export function hzmjTileFace(tile: number): HzmjTileFace {
  if (tile < 0 || tile > 33) {
    return { id: tile, num: '?', kind: '', suit: 'zi', label: '?' }
  }
  if (tile <= 8) {
    return { id: tile, num: String(tile + 1), kind: '万', suit: 'wan', label: tileLabel(tile) }
  }
  if (tile <= 17) {
    return { id: tile, num: String(tile - 8), kind: '条', suit: 'tiao', label: tileLabel(tile) }
  }
  if (tile <= 26) {
    return { id: tile, num: String(tile - 17), kind: '筒', suit: 'tong', label: tileLabel(tile) }
  }
  const text = tileLabel(tile)
  return { id: tile, num: text, kind: '', suit: 'zi', label: text }
}

export function decodeS2C_HzmjGameStart(body: Uint8Array): HzmjGameStart {
  const m = S2C_HzmjGameStart.decode(body)
  return {
    round_id: m.round_id,
    room_id: m.room_id,
    template_id: m.template_id,
    banker_seat: m.banker_seat,
    lian_zhuang: m.lian_zhuang,
    N: m.N,
    caishen: [...m.caishen_tiles],
    self_hand: [...m.self_hand],
    wall_remain: m.wall_remain,
    self_seat: m.self_seat ?? 0,
    base_score: m.base_score,
  }
}

export function decodeS2C_HzmjTurn(body: Uint8Array): {
  seat_id: number
  sub: string
  timeout_s: number
  wall_remain: number
  piao_seat: number
  self_hand: number[]
  can_zimo: boolean
} {
  const m = S2C_HzmjTurn.decode(body)
  return {
    seat_id: m.seat_id ?? 0,
    sub: m.sub,
    timeout_s: m.timeout_s,
    wall_remain: m.wall_remain,
    piao_seat: m.piao_seat,
    self_hand: [...m.self_hand],
    can_zimo: m.can_zimo,
  }
}

export function decodeS2C_HzmjDraw(body: Uint8Array): { seat_id: number; tile: number } {
  const m = S2C_HzmjDraw.decode(body)
  return { seat_id: m.seat_id, tile: m.tile }
}

export function decodeS2C_HzmjDiscardBroadcast(body: Uint8Array): { seat_id: number; tile: number } {
  const m = S2C_HzmjDiscardBroadcast.decode(body)
  return { seat_id: m.seat_id ?? 0, tile: m.tile }
}

export function decodeS2C_HzmjActionBroadcast(body: Uint8Array): {
  seat_id: number
  action: number
  tile: number
  tiles: number[]
  from_seat: number
  meld_kind: number
} {
  const m = S2C_HzmjActionBroadcast.decode(body)
  return {
    seat_id: m.seat_id,
    action: m.action,
    tile: m.tile,
    tiles: [...m.tiles],
    from_seat: m.from_seat ?? -1,
    meld_kind: m.meld_kind,
  }
}

export function decodeS2C_HzmjSettle(body: Uint8Array): HzmjSettle {
  const m = S2C_HzmjSettle.decode(body)
  return {
    round_id: m.round_id,
    winner_seat: m.winner_seat,
    hu_tile: m.hu_tile,
    is_zimo: m.is_zimo,
    shooter_seat: m.shooter_seat,
    M: m.M,
    N: m.N,
    contractor_seat: m.contractor_seat,
    base_score: m.base_score,
    entries: m.entries.map((e) => ({ uid: e.uid, seat_id: e.seat_id, delta_gold: e.delta_gold })),
  }
}

export function decodeS2C_HzmjLiuJu(body: Uint8Array): { lian_zhuang: number } {
  const m = S2C_HzmjLiuJu.decode(body)
  return { lian_zhuang: m.lian_zhuang }
}

const PHZ_SMALL = ['一', '二', '三', '四', '五', '六', '七', '八', '九', '十']
const PHZ_BIG = ['壹', '贰', '叁', '肆', '伍', '陆', '柒', '捌', '玖', '拾']

/** Paohuzi TileId 0..19 */
export function phzTileLabel(tile: number): string {
  if (tile < 0 || tile > 19) return '?'
  if (tile <= 9) return PHZ_SMALL[tile]!
  return PHZ_BIG[tile - 10]!
}

export function phzIsRed(tile: number): boolean {
  const r = tile % 10
  return r === 1 || r === 6 || r === 9
}

export function decodeS2C_PhzGameStart(body: Uint8Array): PhzGameStart {
  const m = S2C_PhzGameStart.decode(body)
  return {
    round_id: m.round_id,
    room_id: m.room_id,
    template_id: m.template_id,
    banker_seat: m.banker_seat,
    self_hand: [...m.self_hand],
    wall_remain: m.wall_remain,
    self_seat: m.self_seat ?? 0,
    base_score: m.base_score,
    cfg_snapshot: m.cfg_snapshot,
  }
}

export function decodeS2C_PhzTurn(body: Uint8Array): {
  seat_id: number
  sub: string
  timeout_s: number
  wall_remain: number
  self_hand: number[]
  can_hu: boolean
} {
  const m = S2C_PhzTurn.decode(body)
  return {
    seat_id: m.seat_id ?? 0,
    sub: m.sub,
    timeout_s: m.timeout_s,
    wall_remain: m.wall_remain,
    self_hand: [...m.self_hand],
    can_hu: m.can_hu,
  }
}

export function decodeS2C_PhzDraw(body: Uint8Array): { seat_id: number; tile: number } {
  const m = S2C_PhzDraw.decode(body)
  return { seat_id: m.seat_id, tile: m.tile }
}

export function decodeS2C_PhzReveal(body: Uint8Array): { seat_id: number; tile: number } {
  const m = S2C_PhzReveal.decode(body)
  return { seat_id: m.seat_id ?? 0, tile: m.tile }
}

export function decodeS2C_PhzDiscardBroadcast(body: Uint8Array): { seat_id: number; tile: number } {
  const m = S2C_PhzDiscardBroadcast.decode(body)
  return { seat_id: m.seat_id ?? 0, tile: m.tile }
}

export function decodeS2C_PhzActionBroadcast(body: Uint8Array): {
  seat_id: number
  action: number
  tile: number
  tiles: number[]
  from_seat: number
  meld_kind: number
} {
  const m = S2C_PhzActionBroadcast.decode(body)
  return {
    seat_id: m.seat_id,
    action: m.action,
    tile: m.tile,
    tiles: [...m.tiles],
    from_seat: m.from_seat ?? -1,
    meld_kind: m.meld_kind,
  }
}

export function decodeS2C_PhzSettle(body: Uint8Array): PhzSettle {
  const m = S2C_PhzSettle.decode(body)
  return {
    round_id: m.round_id,
    winner_seat: m.winner_seat,
    hu_tile: m.hu_tile,
    is_draw_win: m.is_draw_win,
    hu_xi: m.hu_xi,
    tun: m.tun,
    fan: m.fan,
    ming_tang_mask: m.ming_tang_mask,
    base_score: m.base_score,
    entries: m.entries.map((e) => ({ uid: e.uid, seat_id: e.seat_id, delta_gold: e.delta_gold })),
  }
}

export function decodeS2C_PhzLiuJu(body: Uint8Array): { banker_seat: number } {
  const m = S2C_PhzLiuJu.decode(body)
  return { banker_seat: m.banker_seat }
}

export type FishSeatInfo = {
  seat_id: number
  uid: number
  nickname: string
  cannon_mult: number
  online: boolean
  gold: number
  /** Max accepted fire client_seq on this seat (for reconnect resume). */
  last_client_seq: number
}

/** Same as server fish::kSeatCannonPos (origin bottom-left). 0/1 bottom, 2/3 top. */
export const FISH_SEAT_CANNON_POS: ReadonlyArray<{ x: number; y: number }> = [
  { x: 420, y: 70 },
  { x: 1500, y: 70 },
  { x: 420, y: 1050 },
  { x: 1500, y: 1050 },
]

export function fishSeatIsTop(seat: number): boolean {
  return seat === 2 || seat === 3
}

/** Barrel angle in degrees; 0 = straight up (CSS rotate). */
export function fishCannonAngle(cx: number, cy: number, aimX: number, aimY: number): number {
  return (Math.atan2(aimX - cx, aimY - cy) * 180) / Math.PI
}

export function fishCannonAngleFromVel(vx: number, vy: number): number {
  return (Math.atan2(vx, vy) * 180) / Math.PI
}

export type FishSnap = {
  fish_id: number
  type_id: number
  x: number
  y: number
  vx: number
  vy: number
  radius: number
  hp: number
  hp_max: number
}

export type FishVisual = {
  typeId: number
  label: string
  cls: string
  size: number
}

/** Display helper for fish pond UI (type color / size). */
export function fishVisual(typeId: number, radius = 30): FishVisual {
  const map: Record<number, { label: string; cls: string }> = {
    1: { label: '小鱼', cls: 't1' },
    2: { label: '中鱼', cls: 't2' },
    3: { label: '铁甲', cls: 't3' },
  }
  const m = map[typeId] ?? { label: `鱼${typeId}`, cls: 't0' }
  return {
    typeId,
    label: m.label,
    cls: m.cls,
    size: Math.max(22, Math.min(78, Math.round(radius * 0.95))),
  }
}

export type FishGameStart = {
  round_id: number
  room_id: number
  template_id: number
  self_seat: number
  base_score: number
  cannon_mults: number[]
  seats: FishSeatInfo[]
  fish: FishSnap[]
  cfg_snapshot: string
}

export function encodeC2S_FishFire(
  mult: number,
  aim_x: number,
  aim_y: number,
  client_seq: number,
  lock_fish_id = 0,
): Uint8Array {
  return C2S_FishFire.encode({
    mult,
    aim_x,
    aim_y,
    lock_fish_id,
    client_seq,
  }).finish()
}

export function encodeC2S_FishSetMult(mult: number): Uint8Array {
  return C2S_FishSetMult.encode({ mult }).finish()
}

export function encodeC2S_FishLeave(): Uint8Array {
  return C2S_FishLeave.encode({}).finish()
}

export function decodeS2C_FishGameStart(body: Uint8Array): FishGameStart {
  const m = S2C_FishGameStart.decode(body)
  return {
    round_id: m.round_id,
    room_id: m.room_id,
    template_id: m.template_id,
    self_seat: m.self_seat,
    base_score: m.base_score,
    cannon_mults: [...m.cannon_mults],
    seats: m.seats.map((s) => ({
      seat_id: s.seat_id,
      uid: s.uid,
      nickname: s.nickname,
      cannon_mult: s.cannon_mult,
      online: s.online,
      gold: s.gold,
      last_client_seq: Number(s.last_client_seq || 0),
    })),
    fish: m.fish.map((f) => ({
      fish_id: f.fish_id,
      type_id: f.type_id,
      x: f.x,
      y: f.y,
      vx: f.vx,
      vy: f.vy,
      radius: f.radius,
      hp: f.hp,
      hp_max: f.hp_max,
    })),
    cfg_snapshot: m.cfg_snapshot,
  }
}

export function decodeS2C_FishSeatUpdate(body: Uint8Array): FishSeatInfo | null {
  const m = S2C_FishSeatUpdate.decode(body)
  if (!m.seat) return null
  const s = m.seat
  return {
    seat_id: s.seat_id,
    uid: s.uid,
    nickname: s.nickname,
    cannon_mult: s.cannon_mult,
    online: s.online,
    gold: s.gold,
    last_client_seq: Number(s.last_client_seq || 0),
  }
}

export function decodeS2C_FishSpawn(body: Uint8Array): FishSnap[] {
  const m = S2C_FishSpawn.decode(body)
  return m.fish.map((f) => ({
    fish_id: f.fish_id,
    type_id: f.type_id,
    x: f.x,
    y: f.y,
    vx: f.vx,
    vy: f.vy,
    radius: f.radius,
    hp: f.hp,
    hp_max: f.hp_max,
  }))
}

export function decodeS2C_FishDespawn(body: Uint8Array): { fish_ids: number[]; reason: string } {
  const m = S2C_FishDespawn.decode(body)
  return { fish_ids: [...m.fish_ids], reason: m.reason }
}

export function decodeS2C_FishFireBroadcast(body: Uint8Array): {
  seat_id: number
  uid: number
  bullet_id: number
  mult: number
  x: number
  y: number
  vx: number
  vy: number
  client_seq: number
  gold: number
  cost: number
} {
  const m = S2C_FishFireBroadcast.decode(body)
  return {
    seat_id: m.seat_id,
    uid: m.uid,
    bullet_id: m.bullet_id,
    mult: m.mult,
    x: m.x,
    y: m.y,
    vx: m.vx,
    vy: m.vy,
    client_seq: m.client_seq,
    gold: m.gold,
    cost: m.cost,
  }
}

export function decodeS2C_FishHit(body: Uint8Array): {
  bullet_id: number
  fish_id: number
  seat_id: number
  hp: number
  hp_max: number
} {
  const m = S2C_FishHit.decode(body)
  return {
    bullet_id: m.bullet_id,
    fish_id: m.fish_id,
    seat_id: m.seat_id,
    hp: m.hp,
    hp_max: m.hp_max,
  }
}

export function decodeS2C_FishCatch(body: Uint8Array): {
  fish_id: number
  type_id: number
  seat_id: number
  uid: number
  reward: number
  gold: number
} {
  const m = S2C_FishCatch.decode(body)
  return {
    fish_id: m.fish_id,
    type_id: m.type_id,
    seat_id: m.seat_id,
    uid: m.uid,
    reward: m.reward,
    gold: m.gold,
  }
}

export type BijiGameStart = {
  round_id: number
  room_id: number
  template_id: number
  self_seat: number
  players: number
  deal_start: number
  base_score: number
  arrange_timeout_s: number
  hand: number[]
  enable_chixi: boolean
}

export type BijiArrangeState = {
  locked: boolean[]
  trusteeship: boolean[]
  remain_s: number
}

export type BijiSeatSettle = {
  seat_id: number
  uid: number
  dun_delta_head: number
  dun_delta_mid: number
  dun_delta_tail: number
  chixi_delta: number
  gross: number
  rake: number
  net: number
  gold: number
}

export function encodeC2S_BijiArrange(
  head: number[],
  mid: number[],
  tail: number[],
  confirm: boolean,
): Uint8Array {
  return C2S_BijiArrange.encode({ head, mid, tail, confirm }).finish()
}

export function decodeS2C_BijiGameStart(body: Uint8Array): BijiGameStart {
  const m = S2C_BijiGameStart.decode(body)
  return {
    round_id: m.round_id,
    room_id: m.room_id,
    template_id: m.template_id,
    self_seat: m.self_seat,
    players: m.players,
    deal_start: m.deal_start,
    base_score: m.base_score,
    arrange_timeout_s: m.arrange_timeout_s,
    hand: [...m.hand],
    enable_chixi: m.enable_chixi,
  }
}

export function decodeS2C_BijiArrangeState(body: Uint8Array): BijiArrangeState {
  const m = S2C_BijiArrangeState.decode(body)
  return { locked: [...m.locked], trusteeship: [...m.trusteeship], remain_s: m.remain_s }
}

export function decodeS2C_BijiArrangeAck(body: Uint8Array): {
  code: number
  message: string
  locked: boolean
} {
  const m = S2C_BijiArrangeAck.decode(body)
  return { code: m.code, message: m.message, locked: m.locked }
}

export function decodeS2C_BijiCompare(body: Uint8Array) {
  return S2C_BijiCompare.decode(body)
}

export function decodeS2C_BijiSettle(body: Uint8Array): {
  round_id: number
  seats: BijiSeatSettle[]
} {
  const m = S2C_BijiSettle.decode(body)
  return {
    round_id: m.round_id,
    seats: m.seats.map((s) => ({
      seat_id: s.seat_id,
      uid: s.uid,
      dun_delta_head: s.dun_delta_head,
      dun_delta_mid: s.dun_delta_mid,
      dun_delta_tail: s.dun_delta_tail,
      chixi_delta: s.chixi_delta,
      gross: s.gross,
      rake: s.rake,
      net: s.net,
      gold: s.gold,
    })),
  }
}

export function decodeS2C_BijiSnapshot(body: Uint8Array) {
  return S2C_BijiSnapshot.decode(body)
}

/** Poker CardId label: suit*13+rank */
export function bijiCardLabel(id: number): string {
  const suits = ['D', 'C', 'H', 'S']
  const ranks = ['2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A']
  const suit = Math.floor(id / 13)
  const rank = id % 13
  return `${suits[suit] ?? '?'}${ranks[rank] ?? '?'}`
}

export type BijiCardFace = {
  id: number
  rank: string
  suit: string
  red: boolean
  label: string
}

/** Display face for UI: ♦♣♥♠ + rank */
export function bijiCardFace(id: number): BijiCardFace {
  const suits = ['♦', '♣', '♥', '♠']
  const ranks = ['2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K', 'A']
  const suit = Math.floor(id / 13)
  const rank = id % 13
  const red = suit === 0 || suit === 2
  return {
    id,
    rank: ranks[rank] ?? '?',
    suit: suits[suit] ?? '?',
    red,
    label: bijiCardLabel(id),
  }
}

export const BIJI_DUN_TYPE_LABELS = ['散牌', '对子', '顺子', '同花', '同花顺', '三条'] as const
