/** Front-area meld / discard-river helpers (no Vue). */

export type HzmjMeld = {
  kind: number // 1 chi 2 peng 3 ming 4 an 5 bu
  tiles: number[]
  from_seat: number
}

export function resolveMeldKind(action: number, meld_kind: number): number {
  if (meld_kind >= 1 && meld_kind <= 5) return meld_kind
  if (action === 1) return 1
  if (action === 2) return 2
  if (action === 3) return 3
  return 0
}

/** Prefer server tiles; fallback reconstruct peng/gang from claimed tile. */
export function resolveMeldTiles(action: number, tile: number, tiles: number[], meld_kind: number): number[] {
  if (tiles.length > 0) return [...tiles]
  const kind = resolveMeldKind(action, meld_kind)
  if (tile < 0) return []
  if (kind === 2) return [tile, tile, tile]
  if (kind === 3 || kind === 4 || kind === 5) return [tile, tile, tile, tile]
  if (kind === 1) return [tile]
  return []
}

export function applyMeldBroadcast(
  melds: HzmjMeld[],
  action: number,
  tile: number,
  tiles: number[],
  meld_kind: number,
  from_seat: number,
): HzmjMeld[] {
  const kind = resolveMeldKind(action, meld_kind)
  if (kind < 1) return melds
  const faces = resolveMeldTiles(action, tile, tiles, meld_kind)
  if (kind === 5) {
    const next = melds.map((m) => ({ ...m, tiles: [...m.tiles] }))
    for (let i = next.length - 1; i >= 0; --i) {
      if (next[i]!.kind === 2 && next[i]!.tiles[0] === tile) {
        next[i] = { kind: 5, tiles: faces.length ? faces : [tile, tile, tile, tile], from_seat: next[i]!.from_seat }
        return next
      }
    }
    return [...next, { kind: 5, tiles: faces, from_seat }]
  }
  return [...melds, { kind, tiles: faces, from_seat }]
}

export function pushDiscardRiver(river: number[], tile: number): number[] {
  if (tile < 0) return river
  return [...river, tile]
}

/** Reconnect replay of a discard already in the live river: do not append twice. */
export function restoreDiscardRiver(river: number[], tile: number): number[] {
  if (tile < 0) return river
  if (river.length > 0 && river[river.length - 1] === tile) return river
  return pushDiscardRiver(river, tile)
}

/** Claim takes the discarder's last matching tile out of the river. */
export function takeClaimedFromRiver(river: number[], tile: number): number[] {
  if (tile < 0 || river.length === 0) return river
  const i = river.lastIndexOf(tile)
  if (i < 0) return river
  const next = [...river]
  next.splice(i, 1)
  return next
}

export function meldKindLabel(kind: number): string {
  switch (kind) {
    case 1:
      return '吃'
    case 2:
      return '碰'
    case 3:
      return '明杠'
    case 4:
      return '暗杠'
    case 5:
      return '补杠'
    default:
      return ''
  }
}
