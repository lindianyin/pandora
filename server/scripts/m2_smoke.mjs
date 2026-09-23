// M2 smoke: 3 clients match + ready + auto bid/play until settle (Node 18+)
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

function fieldInt(body, field) {
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = Number(readVarint(body, pos))
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = readVarint(body, pos)
      if (fn === field) return Number(v)
    } else if (wt === 2) {
      const len = Number(readVarint(body, pos))
      pos.i += len
    } else break
  }
  return 0
}

function fieldString(body, field) {
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = Number(readVarint(body, pos))
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) readVarint(body, pos)
    else if (wt === 2) {
      const len = Number(readVarint(body, pos))
      const s = body.subarray(pos.i, pos.i + len)
      pos.i += len
      if (fn === field) return s.toString('utf8')
    } else break
  }
  return ''
}

function fieldRepeatedInt(body, field) {
  const out = []
  const pos = { i: 0 }
  while (pos.i < body.length) {
    const tag = Number(readVarint(body, pos))
    const fn = tag >>> 3
    const wt = tag & 7
    if (wt === 0) {
      const v = Number(readVarint(body, pos))
      if (fn === field) out.push(v)
    } else if (wt === 2) {
      const len = Number(readVarint(body, pos))
      const end = pos.i + len
      if (fn === field) while (pos.i < end) out.push(Number(readVarint(body, pos)))
      else pos.i = end
    } else break
  }
  return out
}

async function login(deviceId) {
  const login = await fetch(`${API}/api/v1/auth/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ type: 'guest', device_id: deviceId }),
  }).then((r) => r.json())
  if (login.code !== 0) throw new Error(JSON.stringify(login))
  return login.data
}

function connectPlayer(token, name) {
  return new Promise((resolve, reject) => {
    const state = {
      name,
      uid: 0,
      seat: -1,
      hand: [],
      phase: '',
      turnSeat: -1,
      settled: false,
      sock: null,
      send(msgId, body) {
        state.sock.write(buildWsClientFrame(encodeFrame(msgId, body)))
      },
    }
    const sock = net.connect(WS_PORT, WS_HOST, () => {
      const key = crypto.randomBytes(16).toString('base64')
      sock.write(
        `GET / HTTP/1.1\r\nHost: ${WS_HOST}:${WS_PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
      )
    })
    state.sock = sock
    let buf = Buffer.alloc(0)
    let app = Buffer.alloc(0)
    let upgraded = false
    sock.on('error', reject)
    sock.on('data', (chunk) => {
      buf = Buffer.concat([buf, chunk])
      if (!upgraded) {
        const idx = buf.indexOf('\r\n\r\n')
        if (idx < 0) return
        upgraded = true
        buf = buf.subarray(idx + 4)
        state.send(1, encodeStringField(1, token))
      }
      while (buf.length >= 2) {
        const b1 = buf[1]
        const masked = (b1 & 0x80) !== 0
        let payloadLen = b1 & 0x7f
        let headerLen = 2
        if (payloadLen === 126) {
          if (buf.length < 4) break
          payloadLen = (buf[2] << 8) | buf[3]
          headerLen = 4
        }
        const maskLen = masked ? 4 : 0
        const total = headerLen + maskLen + payloadLen
        if (buf.length < total) break
        let payload = buf.subarray(headerLen + maskLen, total)
        if (masked) {
          const mask = buf.subarray(headerLen, headerLen + 4)
          payload = Buffer.from(payload)
          for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4]
        }
        const opcode = buf[0] & 0x0f
        buf = buf.subarray(total)
        if (opcode === 0x8) return
        if (opcode !== 0x1 && opcode !== 0x2) continue
        app = Buffer.concat([app, payload])
        const parsed = parseAppFrames(app)
        app = Buffer.from(parsed.rest)
        for (const f of parsed.frames) onFrame(state, f.msgId, f.body)
      }
    })
    setTimeout(() => resolve(state), 300)
  })
}

function onFrame(state, msgId, body) {
  if (msgId === 2) {
    state.uid = fieldInt(body, 3)
    console.log(state.name, 'authed', state.uid)
    state.send(1001, Buffer.alloc(0))
    state.send(1010, encodeVarintField(1, 1))
  } else if (msgId === 1012) {
    const st = fieldInt(body, 1)
    console.log(state.name, 'match', st, fieldString(body, 3))
    if (st === 1) state.send(1022, encodeBoolField(1, true))
  } else if (msgId === 1023) {
    console.log(state.name, 'room phase', fieldString(body, 4))
  } else if (msgId === 2001) {
    state.seat = fieldInt(body, 1)
    state.hand = fieldRepeatedInt(body, 2)
    console.log(state.name, 'hand', state.hand.length, 'seat', state.seat, 'landlord', fieldInt(body, 3))
  } else if (msgId === 2002) {
    state.turnSeat = fieldInt(body, 1)
    state.phase = fieldString(body, 2)
    if (state.turnSeat === state.seat) {
      if (state.phase === 'Bid') {
        // first player bid 3 to finish quickly
        const score = state.seat === 0 ? 3 : 0
        state.send(2003, encodeVarintField(1, score))
      } else if (state.phase === 'Play') {
        if (state.hand.length) {
          const c = state.hand[state.hand.length - 1]
          state.send(2005, Buffer.concat([encodeBoolField(1, false), encodeVarintField(2, c)]))
        }
      }
    }
  } else if (msgId === 2006) {
    const seat = fieldInt(body, 1)
    const pass = fieldInt(body, 2)
    const cards = fieldRepeatedInt(body, 3)
    if (seat === state.seat && !pass) {
      for (const c of cards) {
        const i = state.hand.indexOf(c)
        if (i >= 0) state.hand.splice(i, 1)
      }
    }
  } else if (msgId === 2007) {
    state.settled = true
    console.log(state.name, 'SETTLE ok')
  } else if (msgId === 7) {
    const msg = fieldString(body, 2)
    console.log(state.name, 'error', msg)
    if (state.phase === 'Play' && state.turnSeat === state.seat && /cannot beat|invalid|cards not/.test(msg)) {
      state.send(2005, encodeBoolField(1, true))
    }
  }
}

const a = await login('m2-smoke-a')
const b = await login('m2-smoke-b')
const c = await login('m2-smoke-c')
const pa = await connectPlayer(a.access_token, 'A')
const pb = await connectPlayer(b.access_token, 'B')
const pc = await connectPlayer(c.access_token, 'C')

const deadline = Date.now() + 60000
while (Date.now() < deadline) {
  if (pa.settled && pb.settled && pc.settled) {
    console.log('M2 smoke PASS')
    process.exit(0)
  }
  await new Promise((r) => setTimeout(r, 200))
}
console.error('M2 smoke TIMEOUT')
process.exit(1)
