// Quick WSS auth+heartbeat smoke test (Node 18+)
import net from 'node:net'
import crypto from 'node:crypto'

const API = 'http://127.0.0.1:8080'
const WS_HOST = '127.0.0.1'
const WS_PORT = 8081

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
  const tag = Buffer.from([(field << 3) | 2])
  // varint length
  const lenBuf = []
  let n = b.length
  while (n >= 0x80) {
    lenBuf.push((n & 0x7f) | 0x80)
    n >>= 7
  }
  lenBuf.push(n)
  return Buffer.concat([tag, Buffer.from(lenBuf), b])
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

const login = await fetch(`${API}/api/v1/auth/login`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ type: 'guest', device_id: 'node-smoke' }),
}).then((r) => r.json())
if (login.code !== 0) throw new Error(JSON.stringify(login))
const token = login.data.access_token
console.log('login ok', login.data.uid)

await new Promise((resolve, reject) => {
  const sock = net.connect(WS_PORT, WS_HOST, () => {
    const key = crypto.randomBytes(16).toString('base64')
    sock.write(
      `GET / HTTP/1.1\r\nHost: ${WS_HOST}:${WS_PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
    )
  })
  let buf = Buffer.alloc(0)
  let upgraded = false
  sock.on('data', (chunk) => {
    buf = Buffer.concat([buf, chunk])
    if (!upgraded) {
      const idx = buf.indexOf('\r\n\r\n')
      if (idx < 0) return
      upgraded = true
      buf = buf.subarray(idx + 4)
      const authBody = encodeStringField(1, token)
      sock.write(buildWsClientFrame(encodeFrame(1, authBody)))
      setTimeout(() => {
        const hb = Buffer.from([0x08, 0x00]) // empty-ish; server accepts empty heartbeat
        // encode int64 field properly via encodeFrame empty body for heartbeat msg 3
        sock.write(buildWsClientFrame(encodeFrame(3, Buffer.alloc(0))))
      }, 100)
      return
    }
    // parse one server ws frame (unmasked)
    if (buf.length < 2) return
    const opcode = buf[0] & 0x0f
    let payloadLen = buf[1] & 0x7f
    let off = 2
    if (payloadLen === 126) {
      payloadLen = buf.readUInt16BE(2)
      off = 4
    }
    if (buf.length < off + payloadLen) return
    const payload = buf.subarray(off, off + payloadLen)
    buf = buf.subarray(off + payloadLen)
    if (payload.length >= 8) {
      const msgId = payload.readUInt32LE(4)
      console.log('recv app msg_id=', msgId, 'opcode=', opcode)
      if (msgId === 2) {
        console.log('auth result ok')
      }
      if (msgId === 4) {
        console.log('heartbeat ack ok')
        sock.end()
        resolve()
      }
    }
  })
  sock.on('error', reject)
  setTimeout(() => reject(new Error('timeout')), 5000)
})

console.log('smoke pass')
