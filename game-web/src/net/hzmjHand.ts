/** Pure hand / claim UI helpers (no Vue). */

export function sortHand(tiles: number[]): number[] {
  return [...tiles].sort((a, b) => a - b)
}

export function removeOneTile(tiles: number[], tile: number): number[] {
  const i = tiles.lastIndexOf(tile)
  if (i < 0) return tiles
  const next = [...tiles]
  next.splice(i, 1)
  return next
}

/** Idempotent self-discard: skip if tile already absent (reconnect replay). */
export function applySelfDiscard(hand: number[], tile: number): { hand: number[]; changed: boolean } {
  if (!hand.includes(tile)) return { hand, changed: false }
  return { hand: sortHand(removeOneTile(hand, tile)), changed: true }
}

const BAI = 33

function suitBase(t: number): number {
  if (t <= 8) return 0
  if (t <= 17) return 9
  if (t <= 26) return 18
  return -1
}

/** Backtracking melds; 白板 counts as joker. Mirrors server TryMelds. */
function tryMelds(c: number[], jokers: number): boolean {
  let sum = jokers
  for (let i = 0; i < 34; i++) sum += c[i]!
  if (sum === 0) return true
  if (sum % 3 !== 0) return false

  let i = 0
  while (i < 34 && c[i] === 0) i++
  if (i >= 34) return jokers % 3 === 0

  if (c[i]! >= 3) {
    c[i]! -= 3
    if (tryMelds(c, jokers)) {
      c[i]! += 3
      return true
    }
    c[i]! += 3
  }
  if (c[i]! >= 2 && jokers >= 1) {
    c[i]! -= 2
    if (tryMelds(c, jokers - 1)) {
      c[i]! += 2
      return true
    }
    c[i]! += 2
  }
  if (c[i]! >= 1 && jokers >= 2) {
    c[i]! -= 1
    if (tryMelds(c, jokers - 2)) {
      c[i]! += 1
      return true
    }
    c[i]! += 1
  }

  const base = suitBase(i)
  if (base >= 0) {
    const r = i - base
    for (let start = Math.max(0, r - 2); start <= r && start <= 6; start++) {
      if (removeChow(c, jokers, base, start)) return true
    }
  }
  return false
}

function removeChow(c: number[], jokers: number, base: number, r0: number): boolean {
  const need = [0, 0, 0]
  for (let k = 0; k < 3; k++) {
    const t = base + r0 + k
    if (c[t]! > 0) c[t]!--
    else need[k] = 1
  }
  const needJ = need[0]! + need[1]! + need[2]!
  const restore = () => {
    for (let k = 0; k < 3; k++) if (need[k] === 0) c[base + r0 + k]!++
  }
  if (needJ > jokers) {
    restore()
    return false
  }
  if (!tryMelds(c, jokers - needJ)) {
    restore()
    return false
  }
  restore()
  return true
}

function standardWin(counts: number[]): boolean {
  const c = counts.slice()
  const jokers0 = c[BAI]!
  c[BAI] = 0
  let total = jokers0
  for (const n of c) total += n
  // Closed 14, or 11/8/5/2 after exposed melds (pair + 3/2/1/0 sets).
  if (total < 2 || total > 14 || total % 3 !== 2) return false

  for (let p = 0; p < 34; p++) {
    if (p === BAI) continue
    for (let useJ = 0; useJ <= 2; useJ++) {
      const fromTile = 2 - useJ
      if (c[p]! < fromTile || jokers0 < useJ) continue
      c[p]! -= fromTile
      if (tryMelds(c, jokers0 - useJ)) {
        c[p]! += fromTile
        return true
      }
      c[p]! += fromTile
    }
  }
  if (jokers0 >= 2 && tryMelds(c, jokers0 - 2)) return true
  return false
}

function qiDuiWin(counts: number[], meldCount: number): boolean {
  if (meldCount !== 0) return false
  const c = counts.slice()
  const jokers = c[BAI]!
  c[BAI] = 0
  let total = jokers
  for (const n of c) total += n
  if (total !== 14) return false

  let pairSlots = 0
  let pairsNeeded = 0
  for (let i = 0; i < 34; i++) {
    const n = c[i]!
    if (n === 0) continue
    if (n === 4) pairSlots += 2
    else if (n === 3) {
      pairSlots += 1
      pairsNeeded++
    } else if (n === 2) pairSlots += 1
    else if (n === 1) pairsNeeded++
    else return false
  }
  if (pairsNeeded > jokers) return false
  const left = jokers - pairsNeeded
  pairSlots += pairsNeeded
  pairSlots += Math.floor(left / 2)
  return pairSlots === 7 && left % 2 === 0
}

/** True if adding `tile` completes a hu (平胡 / 七对，白板为财神). */
export function wouldHu(hand: number[], tile: number, meldCount = 0): boolean {
  if (tile < 0 || tile >= 34) return false
  const expect = 14 - meldCount * 3
  const tiles = [...hand, tile]
  if (tiles.length !== expect) return false
  const c = new Array<number>(34).fill(0)
  for (const t of tiles) {
    if (t < 0 || t >= 34) return false
    c[t]!++
  }
  if (qiDuiWin(c, meldCount)) return true
  return standardWin(c)
}

/** Current hand already includes the drawn tile (14 / 14-3m) and is a winning shape. */
export function canHuHand(hand: number[], meldCount = 0): boolean {
  const expect = 14 - meldCount * 3
  if (hand.length !== expect) return false
  const c = new Array<number>(34).fill(0)
  for (const t of hand) {
    if (t < 0 || t >= 34) return false
    c[t]!++
  }
  if (qiDuiWin(c, meldCount)) return true
  return standardWin(c)
}

/**
 * 点炮胡按钮：对齐服务端 CanDianpao + WouldHu。
 * - N>=8（三牢）
 * - 非财神打出
 * - 点炮者或胡者一方为庄（闲闲默认不可）
 * - 加上这张牌牌型可胡
 */
export function canShowDianpaoHu(
  N: number,
  claimTile: number,
  mySeat: number,
  discardSeat: number,
  bankerSeat: number,
  caishenTiles: number[] = [33],
  hand?: number[],
  meldCount = 0,
): boolean {
  if (claimTile < 0 || N < 8) return false
  if (caishenTiles.includes(claimTile)) return false
  if (mySeat < 0 || discardSeat < 0 || bankerSeat < 0) return false
  if (mySeat !== bankerSeat && discardSeat !== bankerSeat) return false
  if (hand && !wouldHu(hand, claimTile, meldCount)) return false
  return true
}

export function listAnGangTiles(hand: number[]): number[] {
  const counts = new Map<number, number>()
  for (const t of hand) counts.set(t, (counts.get(t) || 0) + 1)
  return [...counts.entries()].filter(([, n]) => n >= 4).map(([t]) => t)
}

export function listBuGangTiles(
  hand: number[],
  melds: { kind: number; tiles: number[] }[],
): number[] {
  const out: number[] = []
  for (const m of melds) {
    if (m.kind !== 2 || !m.tiles.length) continue
    const t = m.tiles[0]!
    if (hand.includes(t) && !out.includes(t)) out.push(t)
  }
  return out
}
