import { describe, expect, it } from 'vitest'
import {
  MsgId,
  encodeC2S_PhzDiscard,
  encodeC2S_PhzAction,
  encodeFrame,
  tryDecodeFrames,
  phzTileLabel,
  phzIsRed,
  decodeS2C_PhzGameStart,
  decodeS2C_PhzTurn,
  decodeS2C_PhzReveal,
  decodeS2C_PhzSettle,
  decodeS2C_PhzLiuJu,
} from './frame'

function encodeVarint(n: number): number[] {
  const out: number[] = []
  let v = n >>> 0
  while (v >= 0x80) {
    out.push((v & 0x7f) | 0x80)
    v >>>= 7
  }
  out.push(v)
  return out
}

function fieldVarint(fn: number, v: number): number[] {
  return [...encodeVarint((fn << 3) | 0), ...encodeVarint(v)]
}

function fieldString(fn: number, s: string): number[] {
  const bytes = Array.from(new TextEncoder().encode(s))
  return [...encodeVarint((fn << 3) | 2), ...encodeVarint(bytes.length), ...bytes]
}

describe('phz frame', () => {
  it('MsgId 7001-7010', () => {
    expect(MsgId.S2C_PhzGameStart).toBe(7001)
    expect(MsgId.C2S_PhzDiscard).toBe(7005)
    expect(MsgId.S2C_PhzLiuJu).toBe(7010)
  })

  it('phzTileLabel / red', () => {
    expect(phzTileLabel(0)).toBe('一')
    expect(phzTileLabel(9)).toBe('十')
    expect(phzTileLabel(10)).toBe('壹')
    expect(phzTileLabel(19)).toBe('拾')
    expect(phzIsRed(1)).toBe(true)
    expect(phzIsRed(11)).toBe(true)
    expect(phzIsRed(0)).toBe(false)
  })

  it('encode discard/action roundtrip frame', () => {
    const body = encodeC2S_PhzDiscard(5)
    const frame = new Uint8Array(encodeFrame(MsgId.C2S_PhzDiscard, body))
    const { frames } = tryDecodeFrames(frame)
    expect(frames[0]!.msgId).toBe(7005)
    const act = encodeC2S_PhzAction(1, [0, 1])
    expect(act.length).toBeGreaterThan(2)
  })

  it('decode GameStart / Turn / Reveal / Settle / LiuJu', () => {
    const gs = new Uint8Array([
      ...fieldVarint(1, 99),
      ...fieldVarint(2, 7),
      ...fieldVarint(3, 3),
      ...fieldVarint(4, 1),
      ...fieldVarint(5, 0),
      ...fieldVarint(5, 1),
      ...fieldVarint(6, 19),
      ...fieldVarint(7, 2),
      ...fieldVarint(8, 100),
      ...fieldString(9, '{"min_hu_xi":15}'),
    ])
    const start = decodeS2C_PhzGameStart(gs)
    expect(start.round_id).toBe(99)
    expect(start.self_hand).toEqual([0, 1])
    expect(start.self_seat).toBe(2)
    expect(start.cfg_snapshot).toContain('min_hu_xi')

    const turnBody = new Uint8Array([
      ...fieldVarint(1, 0),
      ...fieldString(2, 'discard'),
      ...fieldVarint(3, 12),
      ...fieldVarint(4, 18),
      ...fieldVarint(6, 1),
    ])
    const turn = decodeS2C_PhzTurn(turnBody)
    expect(turn.sub).toBe('discard')
    expect(turn.can_hu).toBe(true)

    const rev = decodeS2C_PhzReveal(new Uint8Array([...fieldVarint(1, 1), ...fieldVarint(2, 5)]))
    expect(rev).toEqual({ seat_id: 1, tile: 5 })

    const settle = decodeS2C_PhzSettle(
      new Uint8Array([
        ...fieldVarint(1, 1),
        ...fieldVarint(2, 0),
        ...fieldVarint(3, 5),
        ...fieldVarint(4, 1),
        ...fieldVarint(5, 18),
        ...fieldVarint(6, 2),
        ...fieldVarint(7, 3),
        ...fieldVarint(8, 1),
        ...fieldVarint(9, 100),
      ]),
    )
    expect(settle.hu_xi).toBe(18)
    expect(settle.tun).toBe(2)
    expect(settle.is_draw_win).toBe(true)

    expect(decodeS2C_PhzLiuJu(new Uint8Array(fieldVarint(1, 2))).banker_seat).toBe(2)
  })
})
