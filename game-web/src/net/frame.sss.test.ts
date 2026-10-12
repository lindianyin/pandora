import { describe, expect, it } from 'vitest'
import {
  MsgId,
  encodeC2S_SssArrange,
  encodeFrame,
  tryDecodeFrames,
  decodeS2C_SssGameStart,
  sssCardLabel,
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

describe('sss frame', () => {
  it('T50 MsgId 700001-700009', () => {
    expect(MsgId.S2C_SssGameStart).toBe(700001)
    expect(MsgId.C2S_SssArrange).toBe(700003)
    expect(MsgId.S2C_SssSettle).toBe(700006)
    expect(MsgId.C2S_SssLeave).toBe(700009)
  })

  it('encode arrange frame', () => {
    const body = encodeC2S_SssArrange(
      [1, 2, 3],
      [4, 5, 6, 7, 8],
      [9, 10, 11, 12, 13],
      false,
      true,
    )
    const frame = new Uint8Array(encodeFrame(MsgId.C2S_SssArrange, body))
    const { frames } = tryDecodeFrames(frame)
    expect(frames[0]!.msgId).toBe(700003)
  })

  it('decode GameStart', () => {
    const gs = new Uint8Array([
      ...fieldVarint(1, 11),
      ...fieldVarint(2, 22),
      ...fieldVarint(4, 1),
      ...fieldVarint(5, 4),
      ...fieldVarint(7, 100),
      ...fieldVarint(9, 12),
      ...fieldVarint(9, 25),
    ])
    const start = decodeS2C_SssGameStart(gs)
    expect(start.round_id).toBe(11)
    expect(start.self_seat).toBe(1)
    expect(start.hand).toEqual([12, 25])
    expect(sssCardLabel(12)).toBe('DA')
  })
})
