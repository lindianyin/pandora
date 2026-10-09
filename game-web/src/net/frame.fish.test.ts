import { describe, expect, it } from 'vitest'
import {
  MsgId,
  encodeC2S_FishFire,
  encodeC2S_FishSetMult,
  encodeFrame,
  tryDecodeFrames,
  decodeS2C_FishGameStart,
  decodeS2C_FishCatch,
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

describe('fish frame', () => {
  it('MsgId 500001-500014', () => {
    expect(MsgId.S2C_FishGameStart).toBe(500001)
    expect(MsgId.C2S_FishFire).toBe(500006)
    expect(MsgId.S2C_FishCatch).toBe(500009)
    expect(MsgId.C2S_FishLock).toBe(500014)
  })

  it('encode fire/setMult frame', () => {
    const body = encodeC2S_FishFire(10, 100, 200, 3)
    const frame = new Uint8Array(encodeFrame(MsgId.C2S_FishFire, body))
    const { frames } = tryDecodeFrames(frame)
    expect(frames[0]!.msgId).toBe(500006)
    expect(encodeC2S_FishSetMult(5).length).toBeGreaterThan(0)
  })

  it('decode GameStart / Catch', () => {
    const gs = new Uint8Array([
      ...fieldVarint(1, 11),
      ...fieldVarint(2, 22),
      ...fieldVarint(3, 4),
      ...fieldVarint(4, 0),
      ...fieldVarint(5, 100),
      ...fieldVarint(6, 1),
      ...fieldVarint(6, 10),
    ])
    const start = decodeS2C_FishGameStart(gs)
    expect(start.round_id).toBe(11)
    expect(start.room_id).toBe(22)
    expect(start.cannon_mults).toEqual([1, 10])

    const catchBody = new Uint8Array([
      ...fieldVarint(1, 9),
      ...fieldVarint(2, 1),
      ...fieldVarint(3, 0),
      ...fieldVarint(4, 42),
      ...fieldVarint(5, 2000),
      ...fieldVarint(6, 8000),
    ])
    const c = decodeS2C_FishCatch(catchBody)
    expect(c.fish_id).toBe(9)
    expect(c.reward).toBe(2000)
    expect(c.gold).toBe(8000)
  })
})
