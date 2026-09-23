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
  S2C_DdzGameStart: 2001,
  S2C_DdzTurn: 2002,
  C2S_DdzBid: 2003,
  S2C_DdzBidBroadcast: 2004,
  C2S_DdzPlay: 2005,
  S2C_DdzPlayBroadcast: 2006,
  S2C_DdzSettle: 2007,
  S2C_DdzReconnect: 2008,
  S2C_ActivityUpdate: 3001,
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

function appendVarint(out: number[], v: number) {
  let n = v >>> 0
  while (n >= 0x80) {
    out.push((n & 0x7f) | 0x80)
    n >>>= 7
  }
  out.push(n)
}

function encodeStringField(fieldNumber: number, value: string): Uint8Array {
  const bytes = new TextEncoder().encode(value)
  const out: number[] = []
  appendVarint(out, (fieldNumber << 3) | 2)
  appendVarint(out, bytes.length)
  for (const b of bytes) out.push(b)
  return new Uint8Array(out)
}

function encodeVarintField(fieldNumber: number, value: number): Uint8Array {
  const out: number[] = []
  appendVarint(out, (fieldNumber << 3) | 0)
  appendVarint(out, value >>> 0)
  return new Uint8Array(out)
}

function encodeInt64Field(fieldNumber: number, value: number): Uint8Array {
  const out: number[] = []
  appendVarint(out, (fieldNumber << 3) | 0)
  let n = BigInt(value)
  if (n < 0n) n = (1n << 64n) + n
  while (n >= 0x80n) {
    out.push(Number(n & 0x7fn) | 0x80)
    n >>= 7n
  }
  out.push(Number(n))
  return new Uint8Array(out)
}

function encodeBoolField(fieldNumber: number, value: boolean): Uint8Array {
  return encodeVarintField(fieldNumber, value ? 1 : 0)
}

function concat(...parts: Uint8Array[]): Uint8Array {
  const n = parts.reduce((s, p) => s + p.length, 0)
  const out = new Uint8Array(n)
  let o = 0
  for (const p of parts) {
    out.set(p, o)
    o += p.length
  }
  return out
}

function readVarint(buf: Uint8Array, pos: { i: number }): number {
  let result = 0
  let shift = 0
  while (pos.i < buf.length) {
    const b = buf[pos.i++]!
    result |= (b & 0x7f) << shift
    if ((b & 0x80) === 0) return result >>> 0
    shift += 7
  }
  return result
}

function readVarintBig(buf: Uint8Array, pos: { i: number }): bigint {
  let result = 0n
  let shift = 0n
  while (pos.i < buf.length) {
    const b = BigInt(buf[pos.i++]!)
    result |= (b & 0x7fn) << shift
    if ((b & 0x80n) === 0n) return result
    shift += 7n
  }
  return result
}

export function encodeC2S_Auth(token: string): Uint8Array {
  return encodeStringField(1, token)
}

export function encodeC2S_Heartbeat(clientTimeMs: number): Uint8Array {
  return encodeInt64Field(1, clientTimeMs)
}

export function encodeC2S_GetLobby(): Uint8Array {
  return new Uint8Array(0)
}

export function encodeC2S_QuickMatch(templateId: number): Uint8Array {
  return encodeVarintField(1, templateId)
}

export function encodeC2S_CancelMatch(): Uint8Array {
  return new Uint8Array(0)
}

export function encodeC2S_Ready(ready: boolean): Uint8Array {
  return encodeBoolField(1, ready)
}

export function encodeC2S_LeaveRoom(): Uint8Array {
  return new Uint8Array(0)
}

export function encodeC2S_DdzBid(score: number): Uint8Array {
  return encodeVarintField(1, score)
}

export function encodeC2S_DdzPlay(pass: boolean, cards: number[]): Uint8Array {
  const parts: Uint8Array[] = [encodeBoolField(1, pass)]
  for (const c of cards) parts.push(encodeVarintField(2, c))
  return concat(...parts)
}

export function decodeS2C_AuthResult(body: Uint8Array): { code: number; message: string; uid: number } {
  let code = 0
  let message = ''
  let uid = 0
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) code = v
      if (fn === 3) uid = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 2) message = new TextDecoder().decode(slice)
    } else break
  }
  return { code, message, uid }
}

export type LobbyTemplate = {
  id: number
  name: string
  base_score: number
  min_gold: number
  max_gold: number
  enabled: boolean
}

function decodeSubMessage(slice: Uint8Array): Record<number, unknown> {
  const fields: Record<number, unknown> = {}
  const pos = { i: 0 }
  while (pos.i < slice.length) {
    const tag = readVarint(slice, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      fields[fn] = readVarintBig(slice, pos)
    } else if (wt === 2) {
      const len = readVarint(slice, pos)
      const s = slice.slice(pos.i, pos.i + len)
      pos.i += len
      fields[fn] = s
    } else break
  }
  return fields
}

function asNum(v: unknown): number {
  if (typeof v === 'bigint') return Number(v)
  if (typeof v === 'number') return v
  return 0
}

function asSigned64(v: unknown): number {
  if (typeof v !== 'bigint' && typeof v !== 'number') return 0
  const u = typeof v === 'bigint' ? v : BigInt(v)
  if (u >= 1n << 63n) return Number(u - (1n << 64n))
  return Number(u)
}

export function decodeS2C_LobbyInfo(body: Uint8Array): {
  templates: LobbyTemplate[]
  gold: number
  diamond: number
} {
  const templates: LobbyTemplate[] = []
  let gold = 0
  let diamond = 0
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 2) gold = v
      if (fn === 3) diamond = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 1) {
        const f = decodeSubMessage(slice)
        templates.push({
          id: asNum(f[1]),
          name: f[2] instanceof Uint8Array ? new TextDecoder().decode(f[2]) : '',
          base_score: asNum(f[3]),
          min_gold: asNum(f[4]),
          max_gold: asNum(f[5]),
          enabled: !!asNum(f[6]),
        })
      }
    } else break
  }
  return { templates, gold, diamond }
}

export function decodeS2C_MatchStatus(body: Uint8Array): { status: number; room_id: number; message: string } {
  let status = 0
  let room_id = 0
  let message = ''
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) status = v
      if (fn === 2) room_id = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 3) message = new TextDecoder().decode(slice)
    } else break
  }
  return { status, room_id, message }
}

export type RoomSeat = {
  seat_id: number
  uid: number
  nickname: string
  ready: boolean
  online: boolean
  trusteeship: boolean
}

export function decodeS2C_RoomState(body: Uint8Array): {
  room_id: number
  template_id: number
  seats: RoomSeat[]
  phase: string
} {
  let room_id = 0
  let template_id = 0
  let phase = ''
  const seats: RoomSeat[] = []
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) room_id = v
      if (fn === 2) template_id = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 3) {
        const f = decodeSubMessage(slice)
        seats.push({
          seat_id: asNum(f[1]),
          uid: asNum(f[2]),
          nickname: f[3] instanceof Uint8Array ? new TextDecoder().decode(f[3]) : '',
          ready: !!asNum(f[4]),
          online: f[5] === undefined ? true : !!asNum(f[5]),
          trusteeship: !!asNum(f[6]),
        })
      } else if (fn === 4) {
        phase = new TextDecoder().decode(slice)
      }
    } else break
  }
  return { room_id, template_id, seats, phase }
}

export function decodeS2C_DdzGameStart(body: Uint8Array): {
  seat_id: number
  hand_cards: number[]
  landlord_seat: number
  bottom_cards: number[]
} {
  let seat_id = 0
  let landlord_seat = -1
  const hand_cards: number[] = []
  const bottom_cards: number[] = []
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const raw = readVarintBig(body, pos)
      const v = Number(raw)
      if (fn === 1) seat_id = v
      if (fn === 2) hand_cards.push(v)
      if (fn === 3) landlord_seat = asSigned64(raw)
      if (fn === 4) bottom_cards.push(v)
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const end = pos.i + len
      if (fn === 2) while (pos.i < end) hand_cards.push(Number(readVarintBig(body, pos)))
      else if (fn === 4) while (pos.i < end) bottom_cards.push(Number(readVarintBig(body, pos)))
      else pos.i = end
    } else break
  }
  return { seat_id, hand_cards, landlord_seat, bottom_cards }
}

export function decodeS2C_DdzTurn(body: Uint8Array): { seat_id: number; phase: string; timeout_s: number } {
  let seat_id = 0
  let phase = ''
  let timeout_s = 0
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) seat_id = v
      if (fn === 3) timeout_s = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 2) phase = new TextDecoder().decode(slice)
    } else break
  }
  return { seat_id, phase, timeout_s }
}

export function decodeS2C_DdzBidBroadcast(body: Uint8Array): { seat_id: number; score: number } {
  let seat_id = 0
  let score = 0
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) seat_id = v
      if (fn === 2) score = v
    } else break
  }
  return { seat_id, score }
}

export function decodeS2C_DdzPlayBroadcast(body: Uint8Array): {
  seat_id: number
  pass: boolean
  cards: number[]
  cards_left: number
} {
  let seat_id = 0
  let pass = false
  let cards_left = 0
  const cards: number[] = []
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) seat_id = v
      if (fn === 2) pass = !!v
      if (fn === 3) cards.push(v)
      if (fn === 4) cards_left = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const end = pos.i + len
      if (fn === 3) while (pos.i < end) cards.push(Number(readVarintBig(body, pos)))
      else pos.i = end
    } else break
  }
  return { seat_id, pass, cards, cards_left }
}

export function decodeS2C_DdzSettle(body: Uint8Array): {
  round_id: number
  base_score: number
  multiplier: number
  entries: { uid: number; seat_id: number; delta_gold: number }[]
} {
  let round_id = 0
  let base_score = 0
  let multiplier = 0
  const entries: { uid: number; seat_id: number; delta_gold: number }[] = []
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      // signed int64 for delta stored as uint varint — for round_id etc positive OK
      if (fn === 1) round_id = v
      if (fn === 2) base_score = v
      if (fn === 3) multiplier = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 4) {
        const f = decodeSubMessage(slice)
        entries.push({
          uid: asNum(f[1]),
          seat_id: asNum(f[2]),
          delta_gold: asSigned64(f[3]),
        })
      }
    } else break
  }
  return { round_id, base_score, multiplier, entries }
}

export function decodeS2C_DdzReconnect(body: Uint8Array): {
  seat_id: number
  phase: string
  hand: number[]
  landlord_seat: number
  current_seat: number
  timeout_s: number
} {
  let seat_id = 0
  let phase = ''
  let landlord_seat = -1
  let current_seat = -1
  let timeout_s = 0
  const hand: number[] = []
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const raw = readVarintBig(body, pos)
      const v = Number(raw)
      if (fn === 1) seat_id = v
      if (fn === 3) hand.push(v)
      if (fn === 4) landlord_seat = asSigned64(raw)
      if (fn === 5) current_seat = asSigned64(raw)
      if (fn === 6) timeout_s = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      if (fn === 2) {
        phase = new TextDecoder().decode(slice)
        pos.i += len
      } else if (fn === 3) {
        const end = pos.i + len
        while (pos.i < end) hand.push(Number(readVarintBig(body, pos)))
      } else {
        pos.i += len
      }
    } else break
  }
  return { seat_id, phase, hand, landlord_seat, current_seat, timeout_s }
}

export function decodeS2C_ActivityUpdate(body: Uint8Array): {
  activity_id: number
  type: string
  progress_json: string
  claimable: boolean
} {
  let activity_id = 0
  let type = ''
  let progress_json = '{}'
  let claimable = false
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) activity_id = v
      if (fn === 4) claimable = !!v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 2) type = new TextDecoder().decode(slice)
      if (fn === 3) progress_json = new TextDecoder().decode(slice)
    } else break
  }
  return { activity_id, type, progress_json, claimable }
}

export function decodeS2C_Error(body: Uint8Array): { code: number; message: string; ref_msg_id: number } {
  let code = 0
  let message = ''
  let ref_msg_id = 0
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = readVarint(body, pos)
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarintBig(body, pos))
      if (fn === 1) code = v
      if (fn === 3) ref_msg_id = v
    } else if (wt === 2) {
      const len = readVarint(body, pos)
      const slice = body.slice(pos.i, pos.i + len)
      pos.i += len
      if (fn === 2) message = new TextDecoder().decode(slice)
    } else break
  }
  return { code, message, ref_msg_id }
}

/** Card display: 0-51 ranks 3..2, 52/53 jokers */
export function cardLabel(card: number): string {
  if (card === 52) return '小王'
  if (card === 53) return '大王'
  const ranks = ['3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K', 'A', '2']
  const suits = ['♠', '♥', '♣', '♦']
  return suits[Math.floor(card / 13) % 4]! + ranks[card % 13]!
}

