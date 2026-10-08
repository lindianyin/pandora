/** Client-side PHZ claim helpers (mirror server game/phz/meld). */

const TILE_KINDS = 20

function isValidTile(t: number) {
  return t >= 0 && t < TILE_KINDS
}

function isSmall(t: number) {
  return t >= 0 && t <= 9
}

function rank(t: number) {
  return isValidTile(t) ? t % 10 : -1
}

function countHand(hand: number[]): number[] {
  const c = new Array(TILE_KINDS).fill(0)
  for (const t of hand) {
    if (isValidTile(t)) c[t]!++
  }
  return c
}

function isSpecialChi(a: number, b: number, claim: number) {
  const s = [a, b, claim].sort((x, y) => x - y)
  const eq = (x: number, y: number, z: number) => s[0] === x && s[1] === y && s[2] === z
  return eq(0, 1, 2) || eq(10, 11, 12) || eq(1, 6, 9) || eq(11, 16, 19)
}

function tryChiPair(handCounts: number[], a: number, b: number, claim: number): boolean {
  if (!isValidTile(a) || !isValidTile(b) || a === claim || b === claim) return false
  const needB = a === b ? 2 : 1
  if ((handCounts[a] || 0) < 1 || (handCounts[b] || 0) < needB) return false
  const three = [a, b, claim].sort((x, y) => x - y)
  if (isSmall(three[0]!) === isSmall(three[1]!) && isSmall(three[1]!) === isSmall(three[2]!)) {
    if (rank(three[0]!) + 1 === rank(three[1]!) && rank(three[1]!) + 1 === rank(three[2]!)) return true
  }
  return isSpecialChi(a, b, claim)
}

export function canPengClaim(hand: number[], claim: number) {
  if (!isValidTile(claim)) return false
  let n = 0
  for (const t of hand) if (t === claim) n++
  if (n < 2) return false
  // Emptying hand requires a real hu path (server enforces 7 melds + min xi).
  if (hand.length === 2) return false
  return true
}

export function canChiForm(hand: number[], claim: number) {
  if (!isValidTile(claim)) return false
  const counts = countHand(hand)
  for (let a = 0; a < TILE_KINDS; a++) {
    if (counts[a]! <= 0) continue
    for (let b = a; b < TILE_KINDS; b++) {
      if (tryChiPair(counts, a, b, claim)) return true
    }
  }
  return false
}

/** Chi only for next seat after discard/reveal source (3 players). */
export function canChiClaim(
  hand: number[],
  claim: number,
  mySeat: number,
  fromSeat: number,
  seatCount = 3,
) {
  if (mySeat < 0 || fromSeat < 0 || seatCount <= 0) return false
  if ((fromSeat + 1) % seatCount !== mySeat) return false
  if (!canChiForm(hand, claim)) return false
  // Using last two tiles for chi empties hand; only legal if it is a winning hu (server-side).
  // Hide button when hand has exactly 2 tiles to avoid illegal-discard stuck state.
  if (hand.length === 2) return false
  return true
}
