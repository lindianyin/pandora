/** Format DB datetime string or unix seconds/ms into `YYYY-MM-DD HH:mm:ss`. */
export function fmtTime(v: unknown): string {
  if (v == null || v === '') return '-'
  if (typeof v === 'string') {
    const s = v.trim()
    if (!s) return '-'
    // already formatted
    if (/^\d{4}-\d{2}-\d{2}/.test(s)) {
      return s.replace('T', ' ').replace(/\.\d+Z?$/, '').slice(0, 19)
    }
    const n = Number(s)
    if (!Number.isNaN(n) && n > 0) return fmtTime(n)
    return s
  }
  if (typeof v === 'number' && Number.isFinite(v) && v > 0) {
    const ms = v < 1e12 ? v * 1000 : v
    const d = new Date(ms)
    if (Number.isNaN(d.getTime())) return '-'
    const pad = (n: number) => String(n).padStart(2, '0')
    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
  }
  return String(v)
}

export function qs(params: Record<string, string | number | undefined | null>): string {
  const parts: string[] = []
  for (const [k, v] of Object.entries(params)) {
    if (v === undefined || v === null || v === '') continue
    parts.push(`${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`)
  }
  return parts.length ? `?${parts.join('&')}` : ''
}
