import { describe, expect, it } from 'vitest'
import {
  MsgId,
  encodeFrame,
  tryDecodeFrames,
  tileLabel,
  encodeC2S_HzmjDiscard,
  encodeC2S_HzmjAction,
  encodeC2S_HzmjGang,
  decodeS2C_HzmjGameStart,
  decodeS2C_HzmjTurn,
  decodeS2C_HzmjDraw,
  decodeS2C_HzmjDiscardBroadcast,
  decodeS2C_HzmjActionBroadcast,
  decodeS2C_HzmjSettle,
  decodeS2C_HzmjLiuJu,
} from './frame'
import { concat, encBytes, encPackedInt32, encString, encVarint } from './pbTestUtil'

describe('frame wire', () => {
  it('encodeFrame + tryDecodeFrames round-trip', () => {
    const body = new Uint8Array([1, 2, 3])
    const buf = new Uint8Array(encodeFrame(MsgId.S2C_HzmjTurn, body))
    const { frames, rest } = tryDecodeFrames(buf)
    expect(rest.length).toBe(0)
    expect(frames).toHaveLength(1)
    expect(frames[0]!.msgId).toBe(6002)
    expect([...frames[0]!.body]).toEqual([1, 2, 3])
  })

  it('tryDecodeFrames keeps partial frame as rest', () => {
    const full = new Uint8Array(encodeFrame(1, new Uint8Array([9])))
    const partial = full.slice(0, 4)
    const { frames, rest } = tryDecodeFrames(partial)
    expect(frames).toHaveLength(0)
    expect(rest.length).toBe(4)
  })

  it('tryDecodeFrames drops illegal length', () => {
    const bad = new Uint8Array(8)
    bad[0] = 2 // length < 4
    const { frames, rest } = tryDecodeFrames(bad)
    expect(frames).toHaveLength(0)
    expect(rest.length).toBe(0)
  })
})

describe('tileLabel', () => {
  it('maps TileId 0..33', () => {
    expect(tileLabel(0)).toBe('1万')
    expect(tileLabel(8)).toBe('9万')
    expect(tileLabel(9)).toBe('1条')
    expect(tileLabel(17)).toBe('9条')
    expect(tileLabel(18)).toBe('1筒')
    expect(tileLabel(26)).toBe('9筒')
    expect(tileLabel(27)).toBe('东')
    expect(tileLabel(30)).toBe('北')
    expect(tileLabel(31)).toBe('中')
    expect(tileLabel(32)).toBe('发')
    expect(tileLabel(33)).toBe('白')
    expect(tileLabel(-1)).toBe('?')
    expect(tileLabel(99)).toBe('?')
  })
})

describe('Hzmj C2S encode', () => {
  it('discard / action / gang field layout', () => {
    expect([...encodeC2S_HzmjDiscard(18)]).toEqual([...encVarint(1, 18)])
    expect([...encodeC2S_HzmjAction(1, [0, 1])]).toEqual([
      ...encVarint(1, 1),
      ...encVarint(2, 0),
      ...encVarint(2, 1),
    ])
    expect([...encodeC2S_HzmjGang(0, 5)]).toEqual([...encVarint(1, 0), ...encVarint(2, 5)])
  })
})

describe('decodeS2C_HzmjGameStart', () => {
  it('decodes seat 0 (optional) and packed hand', () => {
    const body = concat(
      encVarint(1, 100),
      encVarint(2, 7),
      encVarint(3, 2),
      encVarint(4, 0),
      encVarint(5, 1),
      encVarint(6, 2),
      encVarint(7, 33),
      encPackedInt32(8, [0, 1, 2, 3]),
      encVarint(9, 78),
      encVarint(10, 0),
      encVarint(11, 100),
    )
    const g = decodeS2C_HzmjGameStart(body)
    expect(g.round_id).toBe(100)
    expect(g.room_id).toBe(7)
    expect(g.banker_seat).toBe(0)
    expect(g.self_seat).toBe(0)
    expect(g.caishen).toEqual([33])
    expect(g.self_hand).toEqual([0, 1, 2, 3])
    expect(g.wall_remain).toBe(78)
    expect(g.N).toBe(2)
  })

  it('defaults self_seat to 0 when field omitted', () => {
    const body = concat(encVarint(1, 1), encVarint(4, 1), encVarint(8, 5))
    const g = decodeS2C_HzmjGameStart(body)
    expect(g.self_seat).toBe(0)
    expect(g.self_hand).toEqual([5])
  })
})

describe('decodeS2C_HzmjTurn', () => {
  it('decodes sub + self_hand sync', () => {
    const body = concat(
      encVarint(1, 0),
      encString(2, 'discard'),
      encVarint(3, 15),
      encVarint(4, 70),
      encVarint(5, -1),
      encPackedInt32(6, [0, 0, 1, 2]),
    )
    const t = decodeS2C_HzmjTurn(body)
    expect(t.seat_id).toBe(0)
    expect(t.sub).toBe('discard')
    expect(t.timeout_s).toBe(15)
    expect(t.wall_remain).toBe(70)
    expect(t.piao_seat).toBe(-1)
    expect(t.self_hand).toEqual([0, 0, 1, 2])
  })
})

describe('decodeS2C_HzmjDraw', () => {
  it('keeps signed tile -1 for others', () => {
    const body = concat(encVarint(1, 2), encVarint(2, -1))
    const d = decodeS2C_HzmjDraw(body)
    expect(d.seat_id).toBe(2)
    expect(d.tile).toBe(-1)
  })
})

describe('decode discard/action/liuju', () => {
  it('discard + action broadcast', () => {
    expect(decodeS2C_HzmjDiscardBroadcast(concat(encVarint(1, 3), encVarint(2, 21)))).toEqual({
      seat_id: 3,
      tile: 21,
    })
    expect(decodeS2C_HzmjActionBroadcast(concat(encVarint(1, 1), encVarint(2, 2), encVarint(3, 0)))).toEqual({
      seat_id: 1,
      action: 2,
      tile: 0,
      tiles: [],
      from_seat: -1,
      meld_kind: 0,
    })
    const rich = decodeS2C_HzmjActionBroadcast(
      concat(encVarint(1, 2), encVarint(2, 1), encVarint(3, 0), encPackedInt32(4, [0, 1, 2]), encVarint(5, 1), encVarint(6, 1)),
    )
    expect(rich.tiles).toEqual([0, 1, 2])
    expect(rich.from_seat).toBe(1)
    expect(rich.meld_kind).toBe(1)
    expect(decodeS2C_HzmjLiuJu(encVarint(1, 3))).toEqual({ lian_zhuang: 3 })
  })
})

describe('decodeS2C_HzmjSettle', () => {
  it('parses entries with signed delta', () => {
    const entry = concat(encVarint(1, 588), encVarint(2, 0), encVarint(3, -200))
    const body = concat(
      encVarint(1, 9),
      encVarint(2, 1),
      encVarint(3, 27),
      encVarint(4, 1),
      encVarint(5, -1),
      encVarint(6, 1),
      encVarint(7, 2),
      encVarint(8, -1),
      encVarint(9, 100),
      encBytes(10, entry),
    )
    const s = decodeS2C_HzmjSettle(body)
    expect(s.winner_seat).toBe(1)
    expect(s.is_zimo).toBe(true)
    expect(s.shooter_seat).toBe(-1)
    expect(s.N).toBe(2)
    expect(s.entries).toEqual([{ uid: 588, seat_id: 0, delta_gold: -200 }])
  })
})
