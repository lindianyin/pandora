/** Client-side claim helpers (mirror server hzmj/meld, fixed_bai caishen). */

const CAISHEN = 33

export function isCaishenTile(t: number) {
  return t === CAISHEN
}

export function isNumbered(t: number) {
  return t >= 0 && t <= 26
}

function suitBase(t: number) {
  if (t <= 8) return 0
  if (t <= 17) return 9
  if (t <= 26) return 18
  return -1
}

function countInHand(hand: number[], tile: number) {
  let n = 0
  for (const t of hand) if (t === tile) n++
  return n
}

export function canPengClaim(hand: number[], discard: number) {
  if (discard < 0 || isCaishenTile(discard)) return false
  return countInHand(hand, discard) >= 2
}

export function canMingGangClaim(hand: number[], discard: number) {
  if (discard < 0 || isCaishenTile(discard)) return false
  return countInHand(hand, discard) >= 3
}

export type ChiPick = { handTiles: [number, number] }

export function listChiOptions(hand: number[], discard: number): ChiPick[] {
  if (discard < 0 || isCaishenTile(discard) || !isNumbered(discard)) return []
  const base = suitBase(discard)
  const r = discard - base
  const shifts: [number, number][] = [
    [1, 2],
    [-1, 1],
    [-2, -1],
  ]
  const out: ChiPick[] = []
  const counts = new Map<number, number>()
  for (const t of hand) counts.set(t, (counts.get(t) || 0) + 1)
  for (const [da, db] of shifts) {
    const a = r + da
    const b = r + db
    if (a < 0 || a > 8 || b < 0 || b > 8) continue
    const ta = base + a
    const tb = base + b
    if (isCaishenTile(ta) || isCaishenTile(tb)) continue
    if ((counts.get(ta) || 0) < 1 || (counts.get(tb) || 0) < 1) continue
    out.push({ handTiles: [ta, tb] })
  }
  return out
}

export function canChiClaim(hand: number[], discard: number, mySeat: number, discardSeat: number, seatCount = 4) {
  if (mySeat < 0 || discardSeat < 0) return false
  if ((discardSeat + 1) % seatCount !== mySeat) return false
  return listChiOptions(hand, discard).length > 0
}
