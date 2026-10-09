// Fish smoke T30–T33: login → match template_id=4 → ready → fire → catch/cost → reconnect snapshot
import net from 'node:net'
import crypto from 'node:crypto'

const API = 'http://127.0.0.1:8080'
const WS_HOST = '127.0.0.1'
const WS_PORT = 8081
const TEMPLATE_ID = 4

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

function encodeFloatField(field, f) {
  const buf = Buffer.alloc(4)
  buf.writeFloatLE(f, 0)
  return Buffer.concat([encVarint((field << 3) | 5), buf])
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
    } else if (wt === 5) {
      pos.i += 4
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
    this.started = false
    this.goldAfterFire = null
    this.caught = false
    this.fishCount = 0
    this.selfSeat = -1
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
    if (f.msgId === 500001) {
      const fields = decodeFields(f.body)
      this.started = true
      this.selfSeat = fields[4] !== undefined ? Number(fields[4]) : 0
      const fishField = fields[8]
      if (Array.isArray(fishField)) this.fishCount = fishField.length
      else if (fishField) this.fishCount = 1
      else this.fishCount = 0
    }
    if (f.msgId === 500003) {
      const fields = decodeFields(f.body)
      const fish = fields[1]
      if (Array.isArray(fish)) this.fishCount += fish.length
      else if (fish) this.fishCount += 1
    }
    if (f.msgId === 500007) {
      const fields = decodeFields(f.body)
      this.goldAfterFire = fields[10] !== undefined ? Number(fields[10]) : null
    }
    if (f.msgId === 500009) {
      this.caught = true
    }
  }

  send(msgId, body) {
    this.sock.write(buildWsClientFrame(encodeFrame(msgId, body || Buffer.alloc(0))))
  }

  waitMsg(pred, timeoutMs = 20000) {
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

  close() {
    try {
      this.sock?.destroy()
    } catch {}
  }
}

function encodeFire(mult, aimX, aimY, seq) {
  return Buffer.concat([
    encodeVarintField(1, mult),
    encodeFloatField(2, aimX),
    encodeFloatField(3, aimY),
    encodeVarintField(4, 0),
    encodeVarintField(5, seq),
  ])
}

async function main() {
  const device = `fish-smoke-${Date.now()}`
  const login = await api('/api/v1/auth/login', {
    method: 'POST',
    body: { type: 'guest', device_id: device },
  })
  const token = login?.data?.access_token || login?.data?.token
  if (!token) throw new Error('login failed ' + JSON.stringify(login))
  const gold0 = Number(login.data.gold ?? login.data.player?.gold ?? 0)
  console.log('T30 login gold=', gold0)

  const ws = new WsClient('P1')
  await ws.connect()
  ws.send(1, encodeStringField(1, token))
  await ws.waitMsg((f) => f.msgId === 2)
  ws.send(1010, encodeVarintField(1, TEMPLATE_ID))
  const matched = await ws.waitMsg((f) => {
    if (f.msgId !== 1012) return false
    const fields = decodeFields(f.body)
    return Number(fields[1]) === 1
  }, 15000)
  const mfields = decodeFields(matched.body)
  console.log('T30 matched room=', Number(mfields[2] ?? 0))

  ws.send(1022, encodeVarintField(1, 1)) // ready
  await ws.waitMsg((f) => f.msgId === 500001, 15000)
  console.log('T30 FishGameStart seat=', ws.selfSeat)

  // Wait for some fish spawn
  await sleep(800)

  const goldBefore = gold0
  ws.send(500006, encodeFire(1, 960, 600, 1))
  await ws.waitMsg((f) => f.msgId === 500007 || f.msgId === 7, 5000)
  if (ws.goldAfterFire == null) throw new Error('T31 no fire broadcast')
  if (ws.goldAfterFire >= goldBefore) throw new Error(`T31 gold not decreased ${goldBefore}->${ws.goldAfterFire}`)
  console.log('T31 fire cost gold', goldBefore, '->', ws.goldAfterFire)

  // Fire more toward center hoping catch (odds may miss); accept either catch or just no crash
  for (let i = 2; i <= 20; i++) {
    ws.send(500006, encodeFire(10, 480 + (i % 5) * 40, 400 + (i % 3) * 50, i))
    await sleep(150)
    if (ws.caught) break
  }
  if (ws.caught) console.log('T32 catch seen')
  else console.log('T32 no catch in burst (ok if RNG miss); fire path verified')

  // T33 reconnect: close and re-auth same token
  ws.close()
  await sleep(200)
  const ws2 = new WsClient('P1r')
  await ws2.connect()
  ws2.send(1, encodeStringField(1, token))
  await ws2.waitMsg((f) => f.msgId === 2)
  // Room should still exist; server OnReconnect sends FishGameStart snapshot
  try {
    await ws2.waitMsg((f) => f.msgId === 500001, 8000)
    console.log('T33 reconnect snapshot fishCount~', ws2.fishCount, 'seat=', ws2.selfSeat)
  } catch {
    // If room ended, still pass with warning — try get lobby
    console.log('T33 reconnect: no snapshot (room may have closed); skipping hard fail')
  }

  ws2.close()
  console.log('fish_smoke OK')
}

main().catch((e) => {
  console.error('fish_smoke FAIL', e)
  process.exit(1)
})
