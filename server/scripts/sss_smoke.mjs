// SSS smoke T50-T52: login x4 -> match template_id=6 -> ready -> GameStart -> confirm arrange -> Settle
import net from 'node:net'
import crypto from 'node:crypto'

const API = 'http://127.0.0.1:8080'
const WS_HOST = '127.0.0.1'
const WS_PORT = 8081
const TEMPLATE_ID = 6

const MSG_GAME_START = 700001
const MSG_ARRANGE = 700003
const MSG_ARRANGE_ACK = 700004
const MSG_SETTLE = 700006

function encVarint(n) {
  const out = []
  n = BigInt(n)
  if (n < 0n) n = (1n << 64n) + n
  while (n >= 0x80n) {
    out.push(Number(n & 0x7fn) | 0x80)
    n >>= 7n
  }
  out.push(Number(n))
  return Buffer.from(out)
}

function encodeFrame(msgId, body) {
  const length = 4 + body.length
  const out = Buffer.alloc(8 + body.length)
  out.writeUInt32LE(length, 0)
  out.writeUInt32LE(msgId, 4)
  Buffer.from(body).copy(out, 8)
  return out
}

function encodeStringField(field, str) {
  const b = Buffer.from(str, 'utf8')
  return Buffer.concat([encVarint((field << 3) | 2), encVarint(b.length), b])
}

function encodeVarintField(field, v) {
  return Buffer.concat([encVarint((field << 3) | 0), encVarint(BigInt(v))])
}

function encodeBoolField(field, v) {
  return encodeVarintField(field, v ? 1 : 0)
}

function encodePackedInt32Field(field, arr) {
  const parts = arr.map((v) => encVarint(BigInt(v)))
  const packed = Buffer.concat(parts)
  return Buffer.concat([encVarint((field << 3) | 2), encVarint(BigInt(packed.length)), packed])
}

function wsMask(payload) {
  const mask = crypto.randomBytes(4)
  const data = Buffer.from(payload)
  for (let i = 0; i < data.length; i++) data[i] ^= mask[i % 4]
  return { mask, data }
}

function buildWsClientFrame(payload) {
  const { mask, data } = wsMask(payload)
  const header = []
  header.push(0x82)
  if (data.length < 126) header.push(0x80 | data.length)
  else {
    header.push(0x80 | 126)
    header.push((data.length >> 8) & 0xff, data.length & 0xff)
  }
  return Buffer.concat([Buffer.from(header), mask, data])
}

function parseAppFrames(buf) {
  const frames = []
  let offset = 0
  while (buf.length - offset >= 8) {
    const length = buf.readUInt32LE(offset)
    const total = 4 + length
    if (buf.length - offset < total) break
    const msgId = buf.readUInt32LE(offset + 4)
    const body = buf.subarray(offset + 8, offset + total)
    frames.push({ msgId, body })
    offset += total
  }
  return { frames, rest: buf.subarray(offset) }
}

function readVarint(buf, pos) {
  let result = 0n
  let shift = 0n
  while (pos.i < buf.length) {
    const b = BigInt(buf[pos.i++])
    result |= (b & 0x7fn) << shift
    if ((b & 0x80n) === 0n) return result
    shift += 7n
  }
  return result
}

function decodeFields(body) {
  const fields = {}
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = Number(readVarint(body, pos))
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = readVarint(body, pos)
      if (fields[fn] === undefined) fields[fn] = v
      else if (Array.isArray(fields[fn])) fields[fn].push(v)
      else fields[fn] = [fields[fn], v]
    } else if (wt === 2) {
      const len = Number(readVarint(body, pos))
      const slice = body.subarray(pos.i, pos.i + len)
      pos.i += len
      if (fields[fn] === undefined) fields[fn] = slice
      else if (Array.isArray(fields[fn])) fields[fn].push(slice)
      else fields[fn] = [fields[fn], slice]
    } else break
  }
  return fields
}

function decodePackedInt32(buf) {
  if (!buf) return []
  if (Array.isArray(buf)) {
    return buf.flatMap((b) => (Buffer.isBuffer(b) ? decodePackedInt32(b) : [Number(b)]))
  }
  const out = []
  const pos = { i: 0 }
  while (pos.i < buf.length) out.push(Number(readVarint(buf, pos)))
  return out
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms))
}

async function api(path, opts = {}) {
  const res = await fetch(API + path, {
    method: opts.method || 'GET',
    headers: { 'Content-Type': 'application/json', ...(opts.headers || {}) },
    body: opts.body ? JSON.stringify(opts.body) : undefined,
  })
  return res.json()
}

function rankOf(c) {
  return c % 13
}
function suitOf(c) {
  return Math.floor(c / 13)
}

function cmpI8(a, b) {
  if (a < b) return -1
  if (a > b) return 1
  return 0
}

function compareFiveKey(a, b) {
  if (a.level !== b.level) return a.level < b.level ? -1 : 1
  for (let i = 0; i < 5; i++) {
    const c = cmpI8(a[`p${i}`], b[`p${i}`])
    if (c) return c
  }
  return 0
}

function compareHeadKey(a, b) {
  if (a.level !== b.level) return a.level < b.level ? -1 : 1
  for (let i = 0; i < 3; i++) {
    const c = cmpI8(a[`p${i}`], b[`p${i}`])
    if (c) return c
  }
  return 0
}

function tryFiveStraightTop(asc) {
  const [a0, a1, a2, a3, a4] = asc
  if (a0 === 0 && a1 === 1 && a2 === 2 && a3 === 3 && a4 === 12) return -1
  if (a1 === a0 + 1 && a2 === a1 + 1 && a3 === a2 + 1 && a4 === a3 + 1) return a4
  return null
}

function evalFive(cards) {
  const c = cards.slice().sort((a, b) => {
    if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
    return suitOf(b) - suitOf(a)
  })
  const cnt = new Array(13).fill(0)
  for (const x of c) cnt[rankOf(x)]++
  const ranksDesc = c.map(rankOf)
  const sameSuit = suitOf(c[0]) === suitOf(c[1]) && suitOf(c[1]) === suitOf(c[2]) && suitOf(c[2]) === suitOf(c[3]) && suitOf(c[3]) === suitOf(c[4])
  let fiveDistinct = true
  for (let r = 0; r < 13; r++) if (cnt[r] > 1) fiveDistinct = false
  let stTop = null
  let isSt = false
  if (fiveDistinct) {
    const asc = []
    for (let r = 0; r < 13; r++) if (cnt[r]) asc.push(r)
    stTop = tryFiveStraightTop(asc)
    isSt = stTop !== null
  }
  const k = { level: 0, p0: -128, p1: -128, p2: -128, p3: -128, p4: -128 }
  let quad = -1,
    trip = -1,
    pairs = [],
    npair = 0
  for (let r = 12; r >= 0; r--) {
    if (cnt[r] === 4) quad = r
    else if (cnt[r] === 3) trip = r
    else if (cnt[r] === 2) pairs[npair++] = r
  }
  if (sameSuit && isSt) {
    k.level = 8
    k.p0 = stTop
    return k
  }
  if (quad >= 0) {
    k.level = 7
    k.p0 = quad
    for (let r = 12; r >= 0; r--) if (cnt[r] === 1) {
      k.p1 = r
      break
    }
    return k
  }
  if (trip >= 0 && npair >= 1) {
    k.level = 6
    k.p0 = trip
    k.p1 = pairs[0]
    return k
  }
  if (sameSuit) {
    k.level = 5
    for (let i = 0; i < 5; i++) k[`p${i}`] = ranksDesc[i]
    return k
  }
  if (isSt) {
    k.level = 4
    k.p0 = stTop
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
  if (npair === 2) {
    k.level = 2
    k.p0 = pairs[0]
    k.p1 = pairs[1]
    for (let r = 12; r >= 0; r--) if (cnt[r] === 1) {
      k.p2 = r
      break
    }
    return k
  }
  if (npair === 1) {
    k.level = 1
    k.p0 = pairs[0]
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
  for (let i = 0; i < 5; i++) k[`p${i}`] = ranksDesc[i]
  return k
}

function evalHead(cards) {
  const c = cards.slice().sort((a, b) => {
    if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
    return suitOf(b) - suitOf(a)
  })
  const r0 = rankOf(c[0]),
    r1 = rankOf(c[1]),
    r2 = rankOf(c[2])
  const k = { level: 0, p0: -128, p1: -128, p2: -128 }
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

function cmpOrderHeadFive(head, five) {
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

function isLegal(head, mid, tail) {
  const hk = evalHead(head)
  const mk = evalFive(mid)
  const tk = evalFive(tail)
  return cmpOrderHeadFive(hk, mk) <= 0 && compareFiveKey(mk, tk) <= 0
}

function naiveArrange(hand) {
  const h = hand.slice().sort((a, b) => a - b)
  return { head: h.slice(0, 3), mid: h.slice(3, 8), tail: h.slice(8, 13) }
}

function autoArrange13(hand) {
  let best = null
  let bestTail = null
  let bestMid = null
  let bestHead = null
  for (let i = 0; i < 13; i++)
    for (let j = i + 1; j < 13; j++)
      for (let k = j + 1; k < 13; k++) {
        const head = [hand[i], hand[j], hand[k]]
        const rem = []
        for (let t = 0; t < 13; t++) if (t !== i && t !== j && t !== k) rem.push(t)
        for (let a = 0; a < 10; a++)
          for (let b = a + 1; b < 10; b++)
            for (let c = b + 1; c < 10; c++)
              for (let d = c + 1; d < 10; d++)
                for (let e = d + 1; e < 10; e++) {
                  const mid = [hand[rem[a]], hand[rem[b]], hand[rem[c]], hand[rem[d]], hand[rem[e]]]
                  const used = new Set([a, b, c, d, e])
                  const tailIdx = []
                  for (let t = 0; t < 10; t++) if (!used.has(t)) tailIdx.push(rem[t])
                  const tail = [hand[tailIdx[0]], hand[tailIdx[1]], hand[tailIdx[2]], hand[tailIdx[3]], hand[tailIdx[4]]]
                  if (!isLegal(head, mid, tail)) continue
                  const kt = evalFive(tail)
                  const km = evalFive(mid)
                  const kh = evalHead(head)
                  let better = !best
                  if (best) {
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
  return best
}

class WsClient {
  constructor(name) {
    this.name = name
    this.queue = []
    this.hand = []
    this.started = false
    this.settled = false
    this.locked = false
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.sock = net.connect(WS_PORT, WS_HOST, () => {
        const key = crypto.randomBytes(16).toString('base64')
        const req =
          `GET / HTTP/1.1\r\nHost: ${WS_HOST}:${WS_PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`
        this.sock.write(req)
      })
      this._buf = Buffer.alloc(0)
      this._wsBuf = Buffer.alloc(0)
      this.sock.on('data', (chunk) => this.onData(chunk))
      this.sock.on('error', reject)
      this._handshake = false
      this._resolveConnect = resolve
    })
  }

  onData(chunk) {
    this._buf = Buffer.concat([this._buf, chunk])
    if (!this._handshake) {
      const s = this._buf.toString('binary')
      const idx = s.indexOf('\r\n\r\n')
      if (idx < 0) return
      this._handshake = true
      this._buf = this._buf.subarray(idx + 4)
      this._resolveConnect?.()
    }
    this.feedWs()
  }

  feedWs() {
    let buf = Buffer.concat([this._wsBuf, this._buf])
    this._buf = Buffer.alloc(0)
    while (buf.length >= 2) {
      const finOpcode = buf[0]
      const opcode = finOpcode & 0x0f
      let len = buf[1] & 0x7f
      let off = 2
      if (len === 126) {
        if (buf.length < 4) break
        len = buf.readUInt16BE(2)
        off = 4
      } else if (len === 127) break
      if (buf.length < off + len) break
      const payload = buf.subarray(off, off + len)
      buf = buf.subarray(off + len)
      if (opcode === 0x2 || opcode === 0x0) {
        const { frames, rest } = parseAppFrames(payload)
        this._partial = this._partial ? Buffer.concat([this._partial, rest]) : rest
        const more = parseAppFrames(this._partial || Buffer.alloc(0))
        this._partial = more.rest
        for (const f of more.frames.length ? more.frames : frames) this.onFrame(f)
        if (!more.frames.length) for (const f of frames) this.onFrame(f)
      }
    }
    this._wsBuf = buf
  }

  onFrame(f) {
    this.queue.push(f)
    if (f.msgId === MSG_GAME_START) {
      const fields = decodeFields(f.body)
      this.hand = decodePackedInt32(fields[9])
      this.started = true
    }
    if (f.msgId === MSG_SETTLE) this.settled = true
    if (f.msgId === MSG_ARRANGE_ACK) {
      const fields = decodeFields(f.body)
      const code = Number(fields[1] ?? 0)
      const locked = Number(fields[3] ?? 0) === 1
      this.lastAckCode = code
      if (code === 0 && locked) this.locked = true
    }
  }

  send(msgId, body) {
    this.sock.write(buildWsClientFrame(encodeFrame(msgId, body || Buffer.alloc(0))))
  }

  waitMsg(pred, timeoutMs = 30000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${this.name} wait timeout`)), timeoutMs)
      const tryOne = () => {
        for (let i = 0; i < this.queue.length; i++) {
          if (pred(this.queue[i])) {
            clearTimeout(timer)
            const f = this.queue.splice(i, 1)[0]
            resolve(f)
            return true
          }
        }
        return false
      }
      if (tryOne()) return
      const iv = setInterval(() => {
        if (tryOne()) clearInterval(iv)
      }, 20)
      setTimeout(() => clearInterval(iv), timeoutMs + 10)
    })
  }

  sendArrange(head, mid, tail, confirm, reportSpecial = false) {
    const body = Buffer.concat([
      encodePackedInt32Field(1, head),
      encodePackedInt32Field(2, mid),
      encodePackedInt32Field(3, tail),
      encodeBoolField(4, reportSpecial),
      encodeBoolField(5, confirm),
    ])
    this.send(MSG_ARRANGE, body)
  }

  async confirmAnyLegal() {
    let arr = naiveArrange(this.hand)
    if (!isLegal(arr.head, arr.mid, arr.tail)) {
      arr = autoArrange13(this.hand)
    }
    if (!arr) throw new Error(`${this.name} auto arrange failed`)
    this.locked = false
    this.lastAckCode = -1
    this.sendArrange(arr.head, arr.mid, arr.tail, true, false)
    await this.waitMsg((f) => f.msgId === MSG_ARRANGE_ACK, 15000)
    if (!this.locked) {
      throw new Error(`${this.name} confirm not locked ack=${this.lastAckCode} hand=${this.hand}`)
    }
  }

  close() {
    try {
      this.sock?.destroy()
    } catch {}
  }
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg)
}

async function main() {
  const health = await api('/health')
  assert(health.code === 0, 'health failed — is pandora-server running?')

  const clients = []
  const stamp = Date.now()
  for (let i = 0; i < 4; i++) {
    const login = await api('/api/v1/auth/login', {
      method: 'POST',
      body: { type: 'guest', device_id: `sss-${stamp}-${i}` },
    })
    assert(login.code === 0, `login ${i}`)
    const token = login.data.access_token
    const c = new WsClient(`p${i}`)
    await c.connect()
    c.send(1, encodeStringField(1, token))
    await c.waitMsg((f) => f.msgId === 2)
    clients.push(c)
  }

  for (const c of clients) c.send(1010, encodeVarintField(1, TEMPLATE_ID))
  for (const c of clients) {
    await c.waitMsg((f) => {
      if (f.msgId !== 1012) return false
      return Number(decodeFields(f.body)[1]) === 1
    }, 20000)
  }
  for (const c of clients) await c.waitMsg((f) => f.msgId === 1023)
  for (const c of clients) c.send(1022, encodeBoolField(1, true))

  const startDeadline = Date.now() + 20000
  while (Date.now() < startDeadline && !clients.every((c) => c.started)) await sleep(50)
  assert(clients.every((c) => c.started), 'missing S2C_SssGameStart')
  console.log('PASS T50 S2C_SssGameStart x4')

  for (const c of clients) await c.confirmAnyLegal()
  console.log('PASS T51 all confirmed')

  const endDeadline = Date.now() + 30000
  while (Date.now() < endDeadline && !clients.every((c) => c.settled)) await sleep(50)
  assert(clients.every((c) => c.settled), 'missing S2C_SssSettle')
  console.log('PASS T52 S2C_SssSettle')

  for (const c of clients) c.close()
  console.log('ALL SSS SMOKE PASS')
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
