/** Ming-tang mask bits aligned with server phz/config.hpp */

export const MT_DIAN_HU = 1 << 0
export const MT_XIAO_HONG = 1 << 1
export const MT_DA_HONG = 1 << 2
export const MT_WU_HU = 1 << 3
export const MT_SHI_BA_XIAO = 1 << 4
export const MT_DUI_DUI = 1 << 5
export const MT_SAN_TI_WU_KAN = 1 << 6

export function mingTangLabels(mask: number): string[] {
  const out: string[] = []
  if (mask & MT_DIAN_HU) out.push('点胡')
  if (mask & MT_XIAO_HONG) out.push('小红胡')
  if (mask & MT_DA_HONG) out.push('大红胡')
  if (mask & MT_WU_HU) out.push('乌胡')
  if (mask & MT_SHI_BA_XIAO) out.push('十八小')
  if (mask & MT_DUI_DUI) out.push('对对胡')
  if (mask & MT_SAN_TI_WU_KAN) out.push('三提五坎')
  return out
}

export function mingTangText(mask: number): string {
  const labels = mingTangLabels(mask)
  return labels.length ? labels.join('、') : '平胡'
}
