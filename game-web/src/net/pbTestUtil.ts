/** Minimal protobuf wire builders for unit tests (not used in production). */

function appendVarint(out: number[], v: number | bigint) {
  let n = typeof v === 'bigint' ? v : BigInt(v >>> 0)
  if (typeof v === 'number' && v < 0) n = (1n << 64n) + BigInt(v)
  while (n >= 0x80n) {
    out.push(Number(n & 0x7fn) | 0x80)
    n >>= 7n
  }
  out.push(Number(n))
}

export function encVarint(field: number, value: number): Uint8Array {
  const out: number[] = []
  appendVarint(out, (field << 3) | 0)
  appendVarint(out, value)
  return new Uint8Array(out)
}

export function encString(field: number, value: string): Uint8Array {
  const bytes = new TextEncoder().encode(value)
  const out: number[] = []
  appendVarint(out, (field << 3) | 2)
  appendVarint(out, bytes.length)
  for (const b of bytes) out.push(b)
  return new Uint8Array(out)
}

export function encPackedInt32(field: number, values: number[]): Uint8Array {
  const packed: number[] = []
  for (const v of values) appendVarint(packed, v)
  const out: number[] = []
  appendVarint(out, (field << 3) | 2)
  appendVarint(out, packed.length)
  out.push(...packed)
  return new Uint8Array(out)
}

export function encBytes(field: number, inner: Uint8Array): Uint8Array {
  const out: number[] = []
  appendVarint(out, (field << 3) | 2)
  appendVarint(out, inner.length)
  out.push(...inner)
  return new Uint8Array(out)
}

export function concat(...parts: Uint8Array[]): Uint8Array {
  const n = parts.reduce((s, p) => s + p.length, 0)
  const out = new Uint8Array(n)
  let o = 0
  for (const p of parts) {
    out.set(p, o)
    o += p.length
  }
  return out
}
