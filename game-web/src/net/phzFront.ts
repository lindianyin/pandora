/** Phz front melds / discard river / reveal helpers (no Vue). */

export type PhzMeld = {
  kind: number // 1 chi 2 peng 3 wei 4 chou_wei 5 pao 6 ti 7 jiao
  tiles: number[]
  from_seat: number
}

export function resolvePhzMeldKind(action: number, meld_kind: number): number {
  if (meld_kind >= 1 && meld_kind <= 7) return meld_kind
  if (action === 1) return 1
  if (action === 2) return 2
  if (action === 3) return 3
  if (action === 5) return 5
  if (action === 6) return 6
  return 0
}

export function applyPhzMeldBroadcast(
  melds: PhzMeld[],
  action: number,
  tile: number,
  tiles: number[],
  meld_kind: number,
  from_seat: number,
): PhzMeld[] {
  const kind = resolvePhzMeldKind(action, meld_kind)
  if (kind < 1) return melds
  const faces = tiles.length ? [...tiles] : kind === 2 || kind === 3 || kind === 4 ? [tile, tile, tile] : [tile, tile, tile, tile]
  // Pao/Ti may upgrade existing peng/wei
  if (kind === 5 || kind === 6) {
    const next = melds.map((m) => ({ ...m, tiles: [...m.tiles] }))
    for (let i = next.length - 1; i >= 0; --i) {
      const k = next[i]!.kind
      if ((k === 2 || k === 3 || k === 4 || k === 8) && next[i]!.tiles[0] === tile) {
        next[i] = { kind, tiles: faces, from_seat: from_seat >= 0 ? from_seat : next[i]!.from_seat }
        return next
      }
    }
    return [...next, { kind, tiles: faces, from_seat }]
  }
  return [...melds, { kind, tiles: faces, from_seat }]
}

export function pushDiscardRiver(river: number[], tile: number): number[] {
  if (tile < 0) return river
  return [...river, tile]
}

export function restoreDiscardRiver(river: number[], tile: number): number[] {
  if (tile < 0) return river
  if (river.length > 0 && river[river.length - 1] === tile) return river
  return pushDiscardRiver(river, tile)
}

export function takeClaimedFromRiver(river: number[], tile: number): number[] {
  if (tile < 0 || river.length === 0) return river
  const i = river.lastIndexOf(tile)
  if (i < 0) return river
  const next = [...river]
  next.splice(i, 1)
  return next
}

export function restoreReveal(current: { seat: number; tile: number } | null, seat: number, tile: number) {
  if (tile < 0) return current
  if (current && current.seat === seat && current.tile === tile) return current
  return { seat, tile }
}

export function meldKindLabel(kind: number): string {
  switch (kind) {
    case 1:
      return '吃'
    case 2:
      return '碰'
    case 3:
      return '偎'
    case 4:
      return '臭偎'
    case 5:
      return '跑'
    case 6:
      return '提'
    case 7:
      return '绞'
    default:
      return ''
  }
}

/** Concealed-looking melds for others (wei / chou_wei / ti show dark). */
export function isDarkMeld(kind: number): boolean {
  return kind === 3 || kind === 4 || kind === 6
}
