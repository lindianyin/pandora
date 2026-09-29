// Hangzhou Mahjong smoke: login×4 → match template_id=2 → ready → GameStart → auto-play to Settle/LiuJu
import net from 'node:net'
import crypto from 'node:crypto'

const API = 'http://127.0.0.1:8080'
const WS_HOST = '127.0.0.1'
const WS_PORT = 8081
const TEMPLATE_ID = 2

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
  return Buffer.concat([encVarint((field << 3) | 0), encVarint(v)])
}

function encodeBoolField(field, v) {
  return encodeVarintField(field, v ? 1 : 0)
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

async function api(path, { method = 'GET', token, body } = {}) {
  const headers = { 'Content-Type': 'application/json' }
  if (token) headers.Authorization = `Bearer ${token}`
  const res = await fetch(`${API}${path}`, {
    method,
    headers,
    body: body ? JSON.stringify(body) : undefined,
  })
  return res.json()
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms))
}

class WsClient {
  constructor(name) {
    this.name = name
    this.sock = null
    this.appBuf = Buffer.alloc(0)
    this.queue = []
    this.seat = -1
    this.hand = []
    this.started = false
    this.ended = false
    this.endKind = ''
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.sock = net.connect({ host: WS_HOST, port: WS_PORT }, () => {
        const key = crypto.randomBytes(16).toString('base64')
        const req =
          `GET / HTTP/1.1\r\nHost: ${WS_HOST}:${WS_PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
          `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`
        this.sock.write(req)
      })
      let upgraded = false
      let hdr = Buffer.alloc(0)
      this.sock.on('data', (chunk) => {
        if (!upgraded) {
          hdr = Buffer.concat([hdr, chunk])
          const idx = hdr.indexOf('\r\n\r\n')
          if (idx < 0) return
          upgraded = true
          const rest = hdr.subarray(idx + 4)
          resolve()
          if (rest.length) this.onWsData(rest)
          return
        }
        this.onWsData(chunk)
      })
      this.sock.on('error', reject)
    })
  }

  onWsData(chunk) {
    let buf = this._wsBuf ? Buffer.concat([this._wsBuf, chunk]) : chunk
    while (buf.length >= 2) {
      const b1 = buf[1]
      let len = b1 & 0x7f
      let off = 2
      if (len === 126) {
        if (buf.length < 4) break
        len = buf.readUInt16BE(2)
        off = 4
      } else if (len === 127) break
      if (buf.length < off + len) break
      const opcode = buf[0] & 0x0f
      const payload = buf.subarray(off, off + len)
      buf = buf.subarray(off + len)
      if (opcode === 0x2 || opcode === 0x0) {
        const merged = Buffer.concat([this.appBuf, payload])
        const { frames, rest } = parseAppFrames(merged)
        this.appBuf = rest
        for (const f of frames) this.onFrame(f)
      }
    }
    this._wsBuf = buf
  }

  onFrame(f) {
    this.queue.push(f)
    this.handle(f)
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
      const clearAll = () => clearInterval(iv)
      timer.unref?.()
      const orig = timer._onTimeout
      // ensure interval cleared on timeout via reject path
      setTimeout(clearAll, timeoutMs + 10)
    })
  }

  handle(f) {
    if (f.msgId === 6001) {
      const fields = decodeFields(f.body)
      // proto3 omits default 0 — missing self_seat means seat 0
      this.seat = fields[10] !== undefined ? Number(fields[10]) : 0
      this.hand = decodePackedInt32(fields[8])
      this.started = true
    }
    if (f.msgId === 6009) {
      this.ended = true
      this.endKind = 'settle'
    }
    if (f.msgId === 6010) {
      this.ended = true
      this.endKind = 'liuju'
    }
    if (f.msgId === 6003) {
      const fields = decodeFields(f.body)
      const seat = Number(fields[1])
      const tile = Number(fields[2])
      if (seat === this.seat && tile >= 0) this.hand.push(tile)
    }
    if (f.msgId === 6002) {
      const fields = decodeFields(f.body)
      const seat = Number(fields[1] ?? 0)
      const subBuf = fields[2]
      const sub = Buffer.isBuffer(subBuf) ? subBuf.toString('utf8') : String(subBuf || '')
      if (sub === 'claim') {
        this.send(6006, encodeVarintField(1, 0))
      } else if (seat === this.seat && (sub === 'discard' || sub === 'piao' || sub === '')) {
        if (this.hand.length) {
          const tile = this.hand[this.hand.length - 1]
          this.send(6004, encodeVarintField(1, tile))
        }
      }
    }
    if (f.msgId === 6005) {
      const fields = decodeFields(f.body)
      const seat = Number(fields[1] ?? 0)
      const tile = Number(fields[2])
      if (seat === this.seat) {
        const i = this.hand.lastIndexOf(tile)
        if (i >= 0) this.hand.splice(i, 1)
      }
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
      body: { type: 'guest', device_id: `hzmj-${stamp}-${i}` },
    })
    assert(login.code === 0, `login ${i}`)
    const token = login.data.access_token
    const c = new WsClient(`p${i}`)
    c.token = token
    await c.connect()
    c.send(1, encodeStringField(1, token))
    await c.waitMsg((f) => f.msgId === 2)
    clients.push(c)
  }

  for (const c of clients) c.send(1010, encodeVarintField(1, TEMPLATE_ID))
  for (const c of clients) {
    await c.waitMsg((f) => {
      if (f.msgId !== 1012) return false
      const fields = decodeFields(f.body)
      return Number(fields[1]) === 1
    }, 15000)
  }
  for (const c of clients) await c.waitMsg((f) => f.msgId === 1023)
  for (const c of clients) c.send(1022, encodeBoolField(1, true))

  const startDeadline = Date.now() + 20000
  while (Date.now() < startDeadline && !clients.every((c) => c.started)) await sleep(50)
  assert(clients.every((c) => c.started), 'missing S2C_HzmjGameStart')
  console.log('PASS S2C_HzmjGameStart x4')

  const endDeadline = Date.now() + 180000
  while (Date.now() < endDeadline && !clients.some((c) => c.ended)) await sleep(50)
  const done = clients.find((c) => c.ended)
  assert(done, 'timeout waiting Settle/LiuJu')
  console.log(done.endKind === 'settle' ? 'PASS S2C_HzmjSettle' : 'PASS S2C_HzmjLiuJu')

  for (const c of clients) c.close()
  console.log('ALL HZMJ SMOKE PASS')
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
