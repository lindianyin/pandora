/** Client-side SSS arrange helpers aligned with server SPEC §4 / AutoArrange. */

const NEG = -128

export type SssArrange = { head: number[]; mid: number[]; tail: number[] }

type FiveKey = { level: number; p0: number; p1: number; p2: number; p3: number; p4: number }
type HeadKey = { level: number; p0: number; p1: number; p2: number }

function rankOf(c: number) {
  return c % 13
}
function suitOf(c: number) {
  return Math.floor(c / 13)
}

function cmpI8(a: number, b: number) {
  if (a < b) return -1
  if (a > b) return 1
  return 0
}

function compareFiveKey(a: FiveKey, b: FiveKey) {
  if (a.level !== b.level) return a.level < b.level ? -1 : 1
  for (const k of ['p0', 'p1', 'p2', 'p3', 'p4'] as const) {
    const c = cmpI8(a[k], b[k])
    if (c) return c
  }
  return 0
}

function compareHeadKey(a: HeadKey, b: HeadKey) {
  if (a.level !== b.level) return a.level < b.level ? -1 : 1
  for (const k of ['p0', 'p1', 'p2'] as const) {
    const c = cmpI8(a[k], b[k])
    if (c) return c
  }
  return 0
}

function tryFiveStraightTop(asc: number[]) {
  const [a0, a1, a2, a3, a4] = asc
  if (a0 === 0 && a1 === 1 && a2 === 2 && a3 === 3 && a4 === 12) return -1
  if (a1 === a0! + 1 && a2 === a1! + 1 && a3 === a2! + 1 && a4 === a3! + 1) return a4!
  return null
}

export function evalFive(cards: number[]): FiveKey {
  const c = cards.slice().sort((a, b) => {
    if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
    return suitOf(b) - suitOf(a)
  })
  const cnt = new Array(13).fill(0)
  for (const x of c) cnt[rankOf(x)]++
  const ranksDesc = c.map(rankOf)
  const sameSuit =
    suitOf(c[0]!) === suitOf(c[1]!) &&
    suitOf(c[1]!) === suitOf(c[2]!) &&
    suitOf(c[2]!) === suitOf(c[3]!) &&
    suitOf(c[3]!) === suitOf(c[4]!)
  let fiveDistinct = true
  for (let r = 0; r < 13; r++) if (cnt[r]! > 1) fiveDistinct = false
  let stTop: number | null = null
  let isSt = false
  if (fiveDistinct) {
    const asc: number[] = []
    for (let r = 0; r < 13; r++) if (cnt[r]) asc.push(r)
    stTop = tryFiveStraightTop(asc)
    isSt = stTop !== null
  }
  const k: FiveKey = { level: 0, p0: NEG, p1: NEG, p2: NEG, p3: NEG, p4: NEG }
  let quad = -1
  let trip = -1
  const pairs: number[] = []
  for (let r = 12; r >= 0; r--) {
    if (cnt[r] === 4) quad = r
    else if (cnt[r] === 3) trip = r
    else if (cnt[r] === 2) pairs.push(r)
  }
  if (sameSuit && isSt) {
    k.level = 8
    k.p0 = stTop!
    return k
  }
  if (quad >= 0) {
    k.level = 7
    k.p0 = quad
    for (let r = 12; r >= 0; r--) {
      if (cnt[r] === 1) {
        k.p1 = r
        break
      }
    }
    return k
  }
  if (trip >= 0 && pairs.length >= 1) {
    k.level = 6
    k.p0 = trip
    k.p1 = pairs[0]!
    return k
  }
  if (sameSuit) {
    k.level = 5
    k.p0 = ranksDesc[0]!
    k.p1 = ranksDesc[1]!
    k.p2 = ranksDesc[2]!
    k.p3 = ranksDesc[3]!
    k.p4 = ranksDesc[4]!
    return k
  }
  if (isSt) {
    k.level = 4
    k.p0 = stTop!
    return k
  }
  if (trip >= 0) {
    k.level = 3
    k.p0 = trip
    let ki = 1
    for (let r = 12; r >= 0; r--) {
      if (cnt[r] === 1) {
        if (ki === 1) k.p1 = r
        else if (ki === 2) k.p2 = r
        ki++
      }
    }
    return k
  }
  if (pairs.length === 2) {
    k.level = 2
    k.p0 = pairs[0]!
    k.p1 = pairs[1]!
    for (let r = 12; r >= 0; r--) {
      if (cnt[r] === 1) {
        k.p2 = r
        break
      }
    }
    return k
  }
  if (pairs.length === 1) {
    k.level = 1
    k.p0 = pairs[0]!
    let ki = 1
    for (let r = 12; r >= 0; r--) {
      if (cnt[r] === 1) {
        if (ki === 1) k.p1 = r
        else if (ki === 2) k.p2 = r
        else if (ki === 3) k.p3 = r
        ki++
      }
    }
    return k
  }
  k.level = 0
  k.p0 = ranksDesc[0]!
  k.p1 = ranksDesc[1]!
  k.p2 = ranksDesc[2]!
  k.p3 = ranksDesc[3]!
  k.p4 = ranksDesc[4]!
  return k
}

export function evalHead(cards: number[]): HeadKey {
  const c = cards.slice().sort((a, b) => {
    if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
    return suitOf(b) - suitOf(a)
  })
  const r0 = rankOf(c[0]!)
  const r1 = rankOf(c[1]!)
  const r2 = rankOf(c[2]!)
  const k: HeadKey = { level: 0, p0: NEG, p1: NEG, p2: NEG }
  if (r0 === r1 && r1 === r2) {
    k.level = 3
    k.p0 = r0
    return k
  }
  if (r0 === r1 || r1 === r2 || r0 === r2) {
    k.level = 1
    if (r0 === r1) {
      k.p0 = r0
      k.p1 = r2
    } else if (r1 === r2) {
      k.p0 = r1
      k.p1 = r0
    } else {
      k.p0 = r0
      k.p1 = r1
    }
    return k
  }
  k.level = 0
  k.p0 = r0
  k.p1 = r1
  k.p2 = r2
  return k
}

function cmpOrderHeadFive(head: HeadKey, five: FiveKey) {
  if (head.level !== five.level) return head.level < five.level ? -1 : 1
  if (head.level === 3) {
    const c = cmpI8(head.p0, five.p0)
    return c || -1
  }
  if (head.level === 1) {
    const c = cmpI8(head.p0, five.p0)
    if (c) return c
    let fk = five.p1
    if (five.p2 > fk) fk = five.p2
    if (five.p3 > fk) fk = five.p3
    const c2 = cmpI8(head.p1, fk)
    return c2 || -1
  }
  let c = cmpI8(head.p0, five.p0)
  if (c) return c
  c = cmpI8(head.p1, five.p1)
  if (c) return c
  c = cmpI8(head.p2, five.p2)
  return c || -1
}

export function isLegalArrange(head: number[], mid: number[], tail: number[]) {
  if (head.length !== 3 || mid.length !== 5 || tail.length !== 5) return false
  const hk = evalHead(head)
  const mk = evalFive(mid)
  const tk = evalFive(tail)
  return cmpOrderHeadFive(hk, mk) <= 0 && compareFiveKey(mk, tk) <= 0
}

export const SSS_SPECIAL_NONE = 0
export const SSS_SPECIAL_LABELS = [
  '',
  '三同花',
  '三顺子',
  '六对半',
  '五对三条',
  '四套三条',
  '凑一色',
  '全小',
  '全大',
  '三分天下',
  '三同花顺',
  '十二皇族',
  '一条龙',
  '至尊清龙',
] as const

function isRed(c: number) {
  const s = suitOf(c)
  return s === 0 || s === 2
}
function isBlack(c: number) {
  const s = suitOf(c)
  return s === 1 || s === 3
}

function threeStraight(h: number[]) {
  const r = [rankOf(h[0]!), rankOf(h[1]!), rankOf(h[2]!)].sort((a, b) => a - b)
  if (r[0] === 0 && r[1] === 1 && r[2] === 12) return true
  return r[1] === r[0]! + 1 && r[2] === r[1]! + 1
}
function threeFlush(h: number[]) {
  return suitOf(h[0]!) === suitOf(h[1]!) && suitOf(h[1]!) === suitOf(h[2]!)
}
function threeSf(h: number[]) {
  return threeFlush(h) && threeStraight(h)
}

type PartNeed = 'flush' | 'straight' | 'sf'

function existsPartition(hand: number[], need: PartNeed) {
  for (let i = 0; i < 13; i++) {
    for (let j = i + 1; j < 13; j++) {
      for (let k = j + 1; k < 13; k++) {
        const head = [hand[i]!, hand[j]!, hand[k]!]
        let headOk = false
        if (need === 'sf') headOk = threeSf(head)
        else if (need === 'straight') headOk = threeStraight(head)
        else headOk = threeFlush(head)
        if (!headOk) continue
        const rem: number[] = []
        for (let t = 0; t < 13; t++) if (t !== i && t !== j && t !== k) rem.push(t)
        for (let a = 0; a < 10; a++) {
          for (let b = a + 1; b < 10; b++) {
            for (let c = b + 1; c < 10; c++) {
              for (let d = c + 1; d < 10; d++) {
                for (let e = d + 1; e < 10; e++) {
                  const mid = [
                    hand[rem[a]!]!,
                    hand[rem[b]!]!,
                    hand[rem[c]!]!,
                    hand[rem[d]!]!,
                    hand[rem[e]!]!,
                  ]
                  const used = new Set([a, b, c, d, e])
                  const tail: number[] = []
                  for (let t = 0; t < 10; t++) if (!used.has(t)) tail.push(hand[rem[t]!]!)
                  const mk = evalFive(mid)
                  const tk = evalFive(tail)
                  let midOk = false
                  let tailOk = false
                  if (need === 'sf') {
                    midOk = mk.level === 8
                    tailOk = tk.level === 8
                  } else if (need === 'straight') {
                    midOk = mk.level === 4 || mk.level === 8
                    tailOk = tk.level === 4 || tk.level === 8
                  } else {
                    midOk = mk.level === 5 || mk.level === 8
                    tailOk = tk.level === 5 || tk.level === 8
                  }
                  if (midOk && tailOk) return true
                }
              }
            }
          }
        }
      }
    }
  }
  return false
}

/** Highest special type for hand; 0 if none. Matches server DetectSpecial. */
export function detectSpecial(hand: number[], enable: boolean): number {
  if (!enable || hand.length !== 13) return SSS_SPECIAL_NONE
  const cnt = new Array(13).fill(0)
  const suitCnt = [0, 0, 0, 0]
  let allRed = true
  let allBlack = true
  for (const c of hand) {
    cnt[rankOf(c)]++
    suitCnt[suitOf(c)]!++
    if (!isRed(c)) allRed = false
    if (!isBlack(c)) allBlack = false
  }
  let ranksFull = true
  for (let r = 0; r < 13; r++) if (cnt[r] !== 1) ranksFull = false
  if (ranksFull) {
    for (let s = 0; s < 4; s++) if (suitCnt[s] === 13) return 13 // NATURAL_DRAGON
    return 12 // DRAGON
  }
  let royals = true
  for (const c of hand) {
    if (rankOf(c) < 9) {
      royals = false
      break
    }
  }
  if (royals) return 11 // TWELVE_ROYALS
  if (existsPartition(hand, 'sf')) return 10 // THREE_SF

  let nquad = 0
  let ntrip = 0
  let npair = 0
  let nsingle = 0
  for (let r = 0; r < 13; r++) {
    if (cnt[r] === 4) nquad++
    else if (cnt[r] === 3) ntrip++
    else if (cnt[r] === 2) npair++
    else if (cnt[r] === 1) nsingle++
  }
  if (nquad === 3 && nsingle === 1) return 9 // THREE_FOUR

  let allBig = true
  let allSmall = true
  for (const c of hand) {
    const r = rankOf(c)
    if (r < 6) allBig = false
    if (r > 6) allSmall = false
  }
  if (allBig) return 8
  if (allSmall) return 7
  if (allRed || allBlack) return 6
  if (ntrip === 4 && nsingle === 1) return 5
  if (ntrip === 1 && npair === 5) return 4
  if (npair === 6 && nsingle === 1) return 3
  if (existsPartition(hand, 'straight')) return 2
  if (existsPartition(hand, 'flush')) return 1
  return SSS_SPECIAL_NONE
}

export function specialLabel(type: number): string {
  if (type <= 0 || type >= SSS_SPECIAL_LABELS.length) return ''
  return SSS_SPECIAL_LABELS[type]!
}

/** Enumerate legal partitions; maximize tail then mid then head (same as server). */
export function autoArrangeHand(hand: number[]): SssArrange {
  if (hand.length !== 13) {
    const h = hand.slice().sort((a, b) => a - b)
    return { head: h.slice(0, 3), mid: h.slice(3, 8), tail: h.slice(8, 13) }
  }
  let best: SssArrange | null = null
  let bestTail: FiveKey | null = null
  let bestMid: FiveKey | null = null
  let bestHead: HeadKey | null = null

  for (let i = 0; i < 13; i++) {
    for (let j = i + 1; j < 13; j++) {
      for (let k = j + 1; k < 13; k++) {
        const head = [hand[i]!, hand[j]!, hand[k]!]
        const rem: number[] = []
        for (let t = 0; t < 13; t++) if (t !== i && t !== j && t !== k) rem.push(t)
        for (let a = 0; a < 10; a++) {
          for (let b = a + 1; b < 10; b++) {
            for (let c = b + 1; c < 10; c++) {
              for (let d = c + 1; d < 10; d++) {
                for (let e = d + 1; e < 10; e++) {
                  const mid = [
                    hand[rem[a]!]!,
                    hand[rem[b]!]!,
                    hand[rem[c]!]!,
                    hand[rem[d]!]!,
                    hand[rem[e]!]!,
                  ]
                  const used = new Set([a, b, c, d, e])
                  const tailIdx: number[] = []
                  for (let t = 0; t < 10; t++) if (!used.has(t)) tailIdx.push(rem[t]!)
                  const tail = [
                    hand[tailIdx[0]!]!,
                    hand[tailIdx[1]!]!,
                    hand[tailIdx[2]!]!,
                    hand[tailIdx[3]!]!,
                    hand[tailIdx[4]!]!,
                  ]
                  if (!isLegalArrange(head, mid, tail)) continue
                  const kt = evalFive(tail)
                  const km = evalFive(mid)
                  const kh = evalHead(head)
                  let better = !best
                  if (best && bestTail && bestMid && bestHead) {
                    const ct = compareFiveKey(kt, bestTail)
                    if (ct > 0) better = true
                    else if (ct === 0) {
                      const cm = compareFiveKey(km, bestMid)
                      if (cm > 0) better = true
                      else if (cm === 0 && compareHeadKey(kh, bestHead) > 0) better = true
                    }
                  }
                  if (better) {
                    best = { head, mid, tail }
                    bestTail = kt
                    bestMid = km
                    bestHead = kh
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (best) return best
  const h = hand.slice().sort((a, b) => a - b)
  return { head: h.slice(0, 3), mid: h.slice(3, 8), tail: h.slice(8, 13) }
}
