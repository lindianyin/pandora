import { describe, expect, it } from 'vitest'
import {
  applyMeldBroadcast,
  meldKindLabel,
  pushDiscardRiver,
  resolveMeldTiles,
  takeClaimedFromRiver,
} from './hzmjFront'

describe('hzmjFront melds', () => {
  it('reconstructs peng/gang when tiles omitted', () => {
    expect(resolveMeldTiles(2, 0, [], 0)).toEqual([0, 0, 0])
    expect(resolveMeldTiles(3, 5, [], 4)).toEqual([5, 5, 5, 5])
  })

  it('prefers server chi tiles', () => {
    expect(resolveMeldTiles(1, 0, [0, 1, 2], 1)).toEqual([0, 1, 2])
  })

  it('bu gang upgrades matching peng', () => {
    let melds = applyMeldBroadcast([], 2, 3, [3, 3, 3], 2, 1)
    melds = applyMeldBroadcast(melds, 3, 3, [3, 3, 3, 3], 5, -1)
    expect(melds).toHaveLength(1)
    expect(melds[0]!.kind).toBe(5)
    expect(melds[0]!.tiles).toEqual([3, 3, 3, 3])
    expect(meldKindLabel(5)).toBe('补杠')
  })
})

describe('hzmjFront discard river', () => {
  it('push and claim remove last match', () => {
    let r = pushDiscardRiver([], 18)
    r = pushDiscardRiver(r, 19)
    r = pushDiscardRiver(r, 18)
    expect(r).toEqual([18, 19, 18])
    r = takeClaimedFromRiver(r, 18)
    expect(r).toEqual([18, 19])
  })
})
