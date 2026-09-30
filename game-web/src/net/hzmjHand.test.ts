import { describe, expect, it } from 'vitest'
import {
  applySelfDiscard,
  canHuHand,
  canShowDianpaoHu,
  listAnGangTiles,
  listBuGangTiles,
  removeOneTile,
  sortHand,
  wouldHu,
} from './hzmjHand'

describe('hzmjHand', () => {
  it('sortHand ascending', () => {
    expect(sortHand([3, 1, 2])).toEqual([1, 2, 3])
  })

  it('removeOneTile removes one occurrence', () => {
    expect(removeOneTile([0, 0, 1], 0)).toEqual([0, 1])
    expect(removeOneTile([1, 2], 9)).toEqual([1, 2])
  })

  it('applySelfDiscard is idempotent on reconnect replay', () => {
    const once = applySelfDiscard([0, 1, 2], 1)
    expect(once.changed).toBe(true)
    expect(once.hand).toEqual([0, 2])
    const again = applySelfDiscard(once.hand, 1)
    expect(again.changed).toBe(false)
    expect(again.hand).toEqual([0, 2])
  })

  it('canShowDianpaoHu requires sanlao + banker involved + non-caishen', () => {
    expect(canShowDianpaoHu(2, 0, 0, 1, 0)).toBe(false)
    expect(canShowDianpaoHu(8, 0, 0, 1, 0)).toBe(true) // banker wins
    expect(canShowDianpaoHu(8, 0, 2, 1, 1)).toBe(true) // banker shoots
    expect(canShowDianpaoHu(8, 0, 2, 3, 1)).toBe(false) // xian-xian
    expect(canShowDianpaoHu(8, 33, 0, 1, 0, [33])).toBe(false) // caishen
    expect(canShowDianpaoHu(8, -1, 0, 1, 0)).toBe(false)
  })

  it('wouldHu: 1-9万+发发发 听东，发不能胡', () => {
    const hand = [0, 1, 2, 3, 4, 5, 6, 7, 8, 27, 32, 32, 32]
    expect(wouldHu(hand, 27)).toBe(true)
    expect(wouldHu(hand, 32)).toBe(false)
    expect(canShowDianpaoHu(8, 32, 0, 1, 1, [33], hand)).toBe(false)
    expect(canShowDianpaoHu(8, 27, 0, 1, 1, [33], hand)).toBe(true)
  })

  it('wouldHu seven pairs and caishen pair', () => {
    expect(wouldHu([0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12], 12)).toBe(true)
    expect(wouldHu([33, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32], 32)).toBe(true)
  })

  it('listAnGangTiles', () => {
    expect(listAnGangTiles([5, 5, 5, 5, 1])).toEqual([5])
    expect(listAnGangTiles([5, 5, 5])).toEqual([])
  })

  it('listBuGangTiles', () => {
    expect(listBuGangTiles([3, 1], [{ kind: 2, tiles: [3, 3, 3] }])).toEqual([3])
    expect(listBuGangTiles([1], [{ kind: 2, tiles: [3, 3, 3] }])).toEqual([])
  })

  it('canHuHand exposed melds: 4 sets + pair, not 3 sets + pair', () => {
    expect(canHuHand([2, 33], 4)).toBe(true)
    expect(canHuHand([2, 2], 4)).toBe(true)
    expect(canHuHand([2, 33], 3)).toBe(false)
  })
})
