import { describe, expect, it } from 'vitest'
import { canChiClaim, canPengClaim } from './phzMeld'

describe('phzMeld', () => {
  it('peng needs two matching tiles', () => {
    expect(canPengClaim([5, 5, 1], 5)).toBe(true)
    expect(canPengClaim([5, 1, 2], 5)).toBe(false)
    expect(canPengClaim([15, 15], 5)).toBe(false)
  })

  it('chi only next seat and consecutive / special', () => {
    // seat0 discarded 5(六); seat1 is next
    expect(canChiClaim([3, 4, 7], 5, 1, 0, 3)).toBe(true) // 四五六
    expect(canChiClaim([4, 6, 0], 5, 1, 0, 3)).toBe(true) // 五六七
    expect(canChiClaim([4, 6], 5, 1, 0, 3)).toBe(false) // would empty hand
    expect(canChiClaim([5, 5, 1], 5, 1, 0, 3)).toBe(false) // pair uses claim itself
    expect(canChiClaim([3, 4, 7], 5, 2, 0, 3)).toBe(false) // not next seat
    expect(canChiClaim([0, 1, 8], 2, 1, 0, 3)).toBe(true) // 一二三 special
    expect(canChiClaim([0, 1], 2, 1, 0, 3)).toBe(false) // empty-hand chi hidden
  })

  it('peng hidden when only two tiles left', () => {
    expect(canPengClaim([5, 5], 5)).toBe(false)
    expect(canPengClaim([5, 5, 1], 5)).toBe(true)
  })
})
