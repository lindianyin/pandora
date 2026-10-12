import { describe, expect, it } from 'vitest'
import { autoArrangeHand, detectSpecial, isLegalArrange, specialLabel } from './sssArrange'

describe('sssArrange', () => {
  it('naive high-first slice can 倒水; autoArrange stays legal', () => {
    // Mixed ranks: sort-slice often puts strong head / weak tail.
    const hand = [12, 25, 38, 0, 13, 1, 14, 2, 3, 4, 5, 6, 7]
    const sorted = hand.slice().sort((a, b) => {
      const ra = a % 13
      const rb = b % 13
      if (ra !== rb) return rb - ra
      return b - a
    })
    const naive = {
      head: sorted.slice(0, 3),
      mid: sorted.slice(3, 8),
      tail: sorted.slice(8, 13),
    }
    expect(isLegalArrange(naive.head, naive.mid, naive.tail)).toBe(false)

    const arr = autoArrangeHand(hand)
    expect(arr.head).toHaveLength(3)
    expect(arr.mid).toHaveLength(5)
    expect(arr.tail).toHaveLength(5)
    expect(isLegalArrange(arr.head, arr.mid, arr.tail)).toBe(true)
  })

  it('autoArrange on ascending ids is legal', () => {
    const hand = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]
    const arr = autoArrangeHand(hand)
    expect(isLegalArrange(arr.head, arr.mid, arr.tail)).toBe(true)
  })

  it('detectSpecial: dragon / all-small / six-pair / disabled', () => {
    const mk = (suit: number, rank: number) => suit * 13 + rank
    const dragon = Array.from({ length: 13 }, (_, r) => mk(r % 4, r))
    expect(detectSpecial(dragon, true)).toBe(12)
    expect(specialLabel(12)).toBe('一条龙')
    expect(detectSpecial(dragon.map((_, r) => mk(3, r)), true)).toBe(13)
    expect(specialLabel(13)).toBe('至尊清龙')

    const allSmall: number[] = []
    for (let s = 0; s < 4; s++) allSmall.push(mk(s, 0))
    for (let s = 0; s < 3; s++) allSmall.push(mk(s, 2))
    for (let s = 0; s < 3; s++) allSmall.push(mk(s, 4))
    for (let s = 0; s < 3; s++) allSmall.push(mk(s, 6))
    expect(detectSpecial(allSmall, true)).toBe(7)
    expect(specialLabel(7)).toBe('全小')

    const sixPair: number[] = []
    for (let r = 0; r < 6; r++) {
      sixPair.push(mk(0, r), mk(1, r))
    }
    sixPair.push(mk(2, 8))
    expect(detectSpecial(sixPair, true)).toBe(3)
    expect(specialLabel(3)).toBe('六对半')

    expect(detectSpecial(dragon, false)).toBe(0)
  })
})
