import { describe, expect, it } from 'vitest'
import {
  applyPhzMeldBroadcast,
  restoreDiscardRiver,
  takeClaimedFromRiver,
  restoreReveal,
  meldKindLabel,
  isDarkMeld,
} from './phzFront'

describe('phzFront', () => {
  it('applies peng and upgrades to pao', () => {
    let melds = applyPhzMeldBroadcast([], 2, 5, [5, 5, 5], 2, 0)
    expect(melds).toHaveLength(1)
    expect(meldKindLabel(melds[0]!.kind)).toBe('碰')
    melds = applyPhzMeldBroadcast(melds, 5, 5, [5, 5, 5, 5], 5, 0)
    expect(melds).toHaveLength(1)
    expect(melds[0]!.kind).toBe(5)
  })

  it('wei is dark', () => {
    expect(isDarkMeld(3)).toBe(true)
    expect(isDarkMeld(6)).toBe(true)
    expect(isDarkMeld(2)).toBe(false)
  })

  it('river restore / claim', () => {
    let r = restoreDiscardRiver([], 5)
    r = restoreDiscardRiver(r, 5)
    expect(r).toEqual([5])
    r = takeClaimedFromRiver(r, 5)
    expect(r).toEqual([])
  })

  it('reveal restore idempotent', () => {
    const a = restoreReveal(null, 1, 5)
    const b = restoreReveal(a, 1, 5)
    expect(b).toBe(a)
  })
})
