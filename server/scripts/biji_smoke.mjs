// Biji smoke T40-T42: login×4 → match template_id=5 → ready → GameStart → confirm AutoArrange → Settle
import net from 'node:net'
import crypto from 'node:crypto'

const API = 'http://127.0.0.1:8080'
const WS_HOST = '127.0.0.1'
const WS_PORT = 8081
const TEMPLATE_ID = 5

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
function evalDun(cards) {
  const c = cards.slice().sort((a, b) => {
    if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
    return suitOf(b) - suitOf(a)
  })
  const r = c.map(rankOf)
  const s = c.map(suitOf)
  const sameSuit = s[0] === s[1] && s[1] === s[2]
  const sortedR = r.slice().sort((a, b) => a - b)
  let stTop = null
  if (sortedR[0] === 0 && sortedR[1] === 1 && sortedR[2] === 12) stTop = -1
  else if (sortedR[1] === sortedR[0] + 1 && sortedR[2] === sortedR[1] + 1) stTop = sortedR[2]
  if (r[0] === r[1] && r[1] === r[2]) return [5, r[0], 0, 0, Math.max(...s), 0, 0]
  if (sameSuit && stTop !== null) return [4, stTop, 0, 0, Math.max(...s), 0, 0]
  if (sameSuit) return [3, r[0], r[1], r[2], s[0], s[1], s[2]]
  if (stTop !== null) return [2, stTop, 0, 0, Math.max(...s), 0, 0]
  if (r[0] === r[1] || r[1] === r[2] || r[0] === r[2]) {
    let pr, kr, ps, ks
    if (r[0] === r[1]) {
      pr = r[0]
      kr = r[2]
      ps = Math.max(s[0], s[1])
      ks = s[2]
    } else if (r[1] === r[2]) {
      pr = r[1]
      kr = r[0]
      ps = Math.max(s[1], s[2])
      ks = s[0]
    } else {
      pr = r[0]
      kr = r[1]
      ps = Math.max(s[0], s[2])
      ks = s[1]
    }
    return [1, pr, kr, 0, ps, ks, 0]
  }
  return [0, r[0], r[1], r[2], s[0], s[1], s[2]]
}
function cmpDun(a, b) {
  const ka = evalDun(a)
  const kb = evalDun(b)
  for (let i = 0; i < ka.length; i++) if (ka[i] !== kb[i]) return ka[i] < kb[i] ? -1 : 1
  return 0
}
function isLegal(h, m, t) {
  return cmpDun(h, m) <= 0 && cmpDun(m, t) <= 0
}
function autoArrange(hand) {
  let best = null
  let bestKey = null
  for (let i = 0; i < 9; i++)
    for (let j = i + 1; j < 9; j++)
      for (let k = j + 1; k < 9; k++) {
        const head = [hand[i], hand[j], hand[k]]
        const rem = []
        for (let t = 0; t < 9; t++) if (t !== i && t !== j && t !== k) rem.push(t)
        for (let a = 0; a < 6; a++)
          for (let b = a + 1; b < 6; b++)
            for (let c = b + 1; c < 6; c++) {
              const mid = [hand[rem[a]], hand[rem[b]], hand[rem[c]]]
              const tailIdx = []
              for (let t = 0; t < 6; t++) if (t !== a && t !== b && t !== c) tailIdx.push(rem[t])
              const tail = [hand[tailIdx[0]], hand[tailIdx[1]], hand[tailIdx[2]]]
              if (!isLegal(head, mid, tail)) continue
              const key = [evalDun(tail), evalDun(mid), evalDun(head)]
              let better = !best
              if (best) {
                for (let p = 0; p < 3 && !better; p++) {
                  for (let q = 0; q < 7; q++) {
                    if (key[p][q] !== bestKey[p][q]) {
                      better = key[p][q] > bestKey[p][q]
                      p = 3
                      break
                    }
                  }
                }
              }
              if (better) {
                best = { head, mid, tail }
                bestKey = key
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
    if (f.msgId === 600001) {
      const fields = decodeFields(f.body)
      this.hand = decodePackedInt32(fields[9])
      this.started = true
    }
    if (f.msgId === 600006) this.settled = true
    if (f.msgId === 600004) {
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

  sendArrange(head, mid, tail, confirm) {
    const body = Buffer.concat([
      encodePackedInt32Field(1, head),
      encodePackedInt32Field(2, mid),
      encodePackedInt32Field(3, tail),
      encodeBoolField(4, confirm),
    ])
    this.send(600003, body)
  }

  async confirmAnyLegal() {
    const arr = autoArrange(this.hand)
    if (!arr) throw new Error(`${this.name} auto arrange failed`)
    this.locked = false
    this.lastAckCode = -1
    this.sendArrange(arr.head, arr.mid, arr.tail, true)
    await this.waitMsg((f) => f.msgId === 600004, 5000)
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
      body: { type: 'guest', device_id: `biji-${stamp}-${i}` },
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
  assert(clients.every((c) => c.started), 'missing S2C_BijiGameStart')
  console.log('PASS T40 S2C_BijiGameStart x4')

  for (const c of clients) await c.confirmAnyLegal()
  console.log('PASS T41 all confirmed')

  const endDeadline = Date.now() + 30000
  while (Date.now() < endDeadline && !clients.every((c) => c.settled)) await sleep(50)
  assert(clients.every((c) => c.settled), 'missing S2C_BijiSettle')
  console.log('PASS T41/T42 S2C_BijiSettle')

  for (const c of clients) c.close()
  console.log('ALL BIJI SMOKE PASS')
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
