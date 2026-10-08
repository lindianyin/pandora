import { describe, expect, it } from 'vitest'
import { mingTangText, MT_DIAN_HU, MT_WU_HU, MT_DA_HONG } from './phzMingTang'

describe('phzMingTang', () => {
  it('labels mask bits', () => {
    expect(mingTangText(MT_DIAN_HU)).toBe('点胡')
    expect(mingTangText(MT_WU_HU)).toBe('乌胡')
    expect(mingTangText(MT_DA_HONG)).toBe('大红胡')
    expect(mingTangText(0)).toBe('平胡')
  })
})
