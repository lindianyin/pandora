// M3 smoke: wallet exchange/pay idempotency + reconnect snapshot (Node 18+)
import net from 'node:net'
import crypto from 'node:crypto'

const API = 'http://127.0.0.1:8080'
const WS_HOST = '127.0.0.1'
const WS_PORT = 8081

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
    if (wt === 0) fields[fn] = readVarint(body, pos)
    else if (wt === 2) {
      const len = Number(readVarint(body, pos))
      fields[fn] = body.subarray(pos.i, pos.i + len)
      pos.i += len
    } else break
  }
  return fields
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
    this.waiters = []
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
      } else if (len === 127) {
        break
      }
      if (buf.length < off + len) break
      const opcode = buf[0] & 0x0f
      const payload = buf.subarray(off, off + len)
      buf = buf.subarray(off + len)
      if (opcode === 0x2 || opcode === 0x0) {
        const merged = Buffer.concat([this.appBuf, payload])
        const { frames, rest } = parseAppFrames(merged)
        this.appBuf = rest
        for (const f of frames) this.pushFrame(f)
      }
    }
    this._wsBuf = buf
  }

  pushFrame(f) {
    if (this.waiters.length) this.waiters.shift()(f)
    else this.queue.push(f)
  }

  send(msgId, body) {
    this.sock.write(buildWsClientFrame(encodeFrame(msgId, body)))
  }

  waitMsg(pred, timeoutMs = 20000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${this.name} wait timeout`)), timeoutMs)
      const check = (f) => {
        if (pred(f)) {
          clearTimeout(timer)
          resolve(f)
          return true
        }
        return false
      }
      while (this.queue.length) {
        if (check(this.queue.shift())) return
      }
      const onFrame = (f) => {
        if (!check(f)) this.waiters.push(onFrame)
      }
      this.waiters.push(onFrame)
    })
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

async function testWalletPay() {
  const login = await api('/api/v1/auth/login', {
    method: 'POST',
    body: { type: 'guest', device_id: `m3-${Date.now()}` },
  })
  assert(login.code === 0, 'login failed')
  const token = login.data.access_token

  const products = await api('/api/v1/pay/products', { token })
  assert(products.code === 0 && products.data.items?.length, 'products empty')
  const pid = products.data.items[0].id
  const expectDiamond = products.data.items[0].diamond + (products.data.items[0].gift_diamond || 0)

  const created = await api('/api/v1/pay/alipay/create', {
    method: 'POST',
    token,
    body: { product_id: pid },
  })
  assert(created.code === 0, 'create order failed')
  const orderId = created.data.order_id

  const pay1 = await api('/api/v1/pay/alipay/sandbox_complete', {
    method: 'POST',
    token,
    body: { order_id: orderId },
  })
  assert(pay1.code === 0, `sandbox_complete failed: ${pay1.message}`)
  const pay2 = await api('/api/v1/pay/alipay/sandbox_complete', {
    method: 'POST',
    token,
    body: { order_id: orderId },
  })
  assert(pay2.code === 0, 'sandbox idempotent failed')

  let profile = await api('/api/v1/player/profile', { token })
  assert(profile.data.diamond === expectDiamond, `diamond want ${expectDiamond} got ${profile.data.diamond}`)

  const orderKey = `m3-ex-${Date.now()}`
  const exAmt = Math.min(10, expectDiamond)
  const ex1 = await api('/api/v1/wallet/exchange', {
    method: 'POST',
    token,
    body: { diamond: exAmt, client_order_id: orderKey },
  })
  assert(ex1.code === 0, 'exchange failed: ' + ex1.message)
  const goldAfter = ex1.data.gold
  const ex2 = await api('/api/v1/wallet/exchange', {
    method: 'POST',
    token,
    body: { diamond: exAmt, client_order_id: orderKey },
  })
  assert(ex2.code === 0 && ex2.data.gold === goldAfter, 'exchange not idempotent')
  profile = await api('/api/v1/player/profile', { token })
  assert(profile.data.diamond === expectDiamond - exAmt, 'diamond double burn')
  console.log('PASS wallet/pay idempotent')
}

async function testReconnect() {
  const clients = []
  for (let i = 0; i < 3; i++) {
    const login = await api('/api/v1/auth/login', {
      method: 'POST',
      body: { type: 'guest', device_id: `m3r-${Date.now()}-${i}` },
    })
    assert(login.code === 0, 'login')
    const c = new WsClient(`c${i}`)
    c.token = login.data.access_token
    await c.connect()
    c.send(1, encodeStringField(1, c.token))
    await c.waitMsg((f) => f.msgId === 2)
    clients.push(c)
  }

  for (const c of clients) c.send(1010, encodeVarintField(1, 1))
  for (const c of clients) await c.waitMsg((f) => f.msgId === 1012 && true)
  // drain room state
  for (const c of clients) {
    await c.waitMsg((f) => f.msgId === 1023)
  }
  for (const c of clients) c.send(1022, encodeBoolField(1, true))
  await clients[0].waitMsg((f) => f.msgId === 2001 || f.msgId === 2002, 25000)

  const victim = clients[0]
  const token = victim.token
  victim.close()
  await sleep(800)

  const again = new WsClient('c0-re')
  await again.connect()
  again.send(1, encodeStringField(1, token))
  await again.waitMsg((f) => f.msgId === 2)

  let gotReconnect = false
  const deadline = Date.now() + 10000
  while (Date.now() < deadline && !gotReconnect) {
    const f = await again.waitMsg(() => true, deadline - Date.now()).catch(() => null)
    if (!f) break
    if (f.msgId === 2008) {
      const fields = decodeFields(f.body)
      assert(fields[3], 'reconnect hand missing')
      gotReconnect = true
    }
  }
  assert(gotReconnect, 'missing S2C_DdzReconnect')
  console.log('PASS reconnect snapshot')

  for (const c of clients.slice(1)) c.close()
  again.close()
}

async function main() {
  const health = await api('/health')
  assert(health.code === 0, 'health failed — is pandora-server running?')
  await testWalletPay()
  await testReconnect()
  console.log('ALL M3 SMOKE PASS')
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
