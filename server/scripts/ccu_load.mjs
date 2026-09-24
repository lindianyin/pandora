// CCU load smoke — login + WSS auth + heartbeat (Node 18+)
// Usage: node ccu_load.mjs --target 200
import net from 'net'

const API = process.env.API || 'http://127.0.0.1:8080'
const WS_HOST = process.env.WS_HOST || '127.0.0.1'
const WS_PORT = Number(process.env.WS_PORT || 8081)
const target = (() => {
  const i = process.argv.indexOf('--target')
  return i >= 0 ? Number(process.argv[i + 1]) : 100
})()

function writeU32LE(buf, off, v) {
  buf[off] = v & 0xff
  buf[off + 1] = (v >>> 8) & 0xff
  buf[off + 2] = (v >>> 16) & 0xff
  buf[off + 3] = (v >>> 24) & 0xff
}

function encodeFrame(msgId, body) {
  const out = Buffer.alloc(8 + body.length)
  writeU32LE(out, 0, 4 + body.length)
  writeU32LE(out, 4, msgId)
  body.copy(out, 8)
  return out
}

function encodeStringField(fn, s) {
  const b = Buffer.from(s, 'utf8')
  const tag = Buffer.from([(fn << 3) | 2])
  const len = []
  let n = b.length
  while (n >= 0x80) {
    len.push((n & 0x7f) | 0x80)
    n >>>= 7
  }
  len.push(n)
  return Buffer.concat([tag, Buffer.from(len), b])
}

function wsMask(payload) {
  const mask = Buffer.from([1, 2, 3, 4])
  const out = Buffer.alloc(payload.length)
  for (let i = 0; i < payload.length; ++i) out[i] = payload[i] ^ mask[i % 4]
  return { mask, data: out }
}

function wsClientFrame(opcode, payload) {
  const { mask, data } = wsMask(payload)
  let header
  if (payload.length < 126) {
    header = Buffer.alloc(2)
    header[0] = 0x80 | opcode
    header[1] = 0x80 | payload.length
  } else {
    header = Buffer.alloc(4)
    header[0] = 0x80 | opcode
    header[1] = 0x80 | 126
    header[2] = (payload.length >> 8) & 0xff
    header[3] = payload.length & 0xff
  }
  return Buffer.concat([header, mask, data])
}

async function login(i) {
  const res = await fetch(`${API}/api/v1/auth/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ type: 'guest', device_id: `ccu-${Date.now()}-${i}` }),
  })
  const j = await res.json()
  if (j.code !== 0) throw new Error('login ' + j.message)
  return j.data
}

function connectWs(token) {
  return new Promise((resolve, reject) => {
    const sock = net.connect(WS_PORT, WS_HOST, () => {
      // RFC6455 sample key (already base64); must not double-encode
      const key = 'dGhlIHNhbXBsZSBub25jZQ=='
      const req =
        `GET / HTTP/1.1\r\nHost: ${WS_HOST}:${WS_PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
        `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`
      sock.write(req)
    })
    let buf = Buffer.alloc(0)
    let upgraded = false
    sock.on('data', (chunk) => {
      buf = Buffer.concat([buf, chunk])
      if (!upgraded) {
        const s = buf.toString('utf8')
        if (s.includes('\r\n\r\n')) {
          if (!s.includes('101')) {
            reject(new Error('ws upgrade fail'))
            sock.destroy()
            return
          }
          upgraded = true
          const authBody = encodeStringField(1, token)
          sock.write(wsClientFrame(0x2, encodeFrame(1, authBody)))
          const hb = setInterval(() => {
            if (sock.destroyed) {
              clearInterval(hb)
              return
            }
            sock.write(wsClientFrame(0x2, encodeFrame(3, Buffer.alloc(0))))
          }, 10000)
          sock.on('close', () => clearInterval(hb))
          resolve(sock)
        }
      }
    })
    sock.on('error', reject)
  })
}

async function main() {
  const health = await fetch(`${API}/health`).then((r) => r.json())
  if (health.code !== 0) throw new Error('health')
  console.log('health ccu=', health.data.ccu, 'iocp_workers=', health.data.iocp_workers)

  const socks = []
  const batch = 20
  for (let i = 0; i < target; i += batch) {
    const n = Math.min(batch, target - i)
    const parts = await Promise.all(
      Array.from({ length: n }, async (_, j) => {
        const u = await login(i + j)
        return connectWs(u.access_token)
      }),
    )
    socks.push(...parts)
    process.stdout.write(`\rconnected ${socks.length}/${target}`)
  }
  console.log('\nholding 8s...')
  await new Promise((r) => setTimeout(r, 8000))
  const h2 = await fetch(`${API}/health`).then((r) => r.json())
  console.log('peak health ccu=', h2.data.ccu)
  for (const s of socks) s.destroy()
  if (h2.data.ccu < Math.min(target * 0.5, target)) {
    throw new Error('ccu too low: ' + h2.data.ccu)
  }
  console.log('CCU LOAD SMOKE PASS target=', target, 'ccu=', h2.data.ccu)
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
