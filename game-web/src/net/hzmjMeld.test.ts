import { describe, expect, it } from 'vitest'
import {
  canChiClaim,
  canMingGangClaim,
  canPengClaim,
  isCaishenTile,
  listChiOptions,
} from './hzmjMeld'

const BAI = 33

describe('hzmjMeld (mirror server T10 / chi)', () => {
  it('caishen is bai=33', () => {
    expect(isCaishenTile(BAI)).toBe(true)
    expect(isCaishenTile(31)).toBe(false)
  })

  it('T10: cannot peng/gang discard bai', () => {
    expect(canPengClaim([BAI, BAI], BAI)).toBe(false)
    expect(canMingGangClaim([BAI, BAI, BAI], BAI)).toBe(false)
  })

  it('peng / ming gang normal tiles', () => {
    expect(canPengClaim([0, 0, 1], 0)).toBe(true)
    expect(canPengClaim([0, 1], 0)).toBe(false)
    expect(canMingGangClaim([0, 0, 0], 0)).toBe(true)
    expect(canMingGangClaim([0, 0], 0)).toBe(false)
  })

  it('chi 123 wan from discard 1wan', () => {
    const opts = listChiOptions([1, 2], 0)
    expect(opts.length).toBeGreaterThan(0)
    expect(opts[0]!.handTiles).toEqual([1, 2])
  })

  it('no chi on bai or winds', () => {
    expect(listChiOptions([1, 2], BAI)).toEqual([])
    expect(listChiOptions([27, 28], 29)).toEqual([])
  })

  it('canChiClaim only from previous seat', () => {
    const hand = [1, 2]
    expect(canChiClaim(hand, 0, 1, 0, 4)).toBe(true)
    expect(canChiClaim(hand, 0, 2, 0, 4)).toBe(false)
    expect(canChiClaim(hand, 0, 0, 3, 4)).toBe(true)
  })
})
