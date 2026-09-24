// Quick HTTPS + WSS smoke (self-signed; rejectUnauthorized=false)
import net from 'net'
import tls from 'tls'
import { Buffer } from 'buffer'

const API = process.env.API || 'https://127.0.0.1:8443'
const WS_HOST = process.env.WS_HOST || '127.0.0.1'
const WS_PORT = Number(process.env.WS_PORT || 8444)

process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0'

function encodeVarint(n) {
  const out = []
  while (n > 0x7f) {
    out.push((n & 0x7f) | 0x80)
    n >>>= 7
  }
  out.push(n)
  return Buffer.from(out)
}

function encodeStringField(fn, s) {
  const data = Buffer.from(s, 'utf8')
  return Buffer.concat([encodeVarint((fn << 3) | 2), encodeVarint(data.length), data])
}

function encodeFrame(msgId, body) {
  const len = Buffer.alloc(4)
  len.writeUInt32LE(4 + body.length, 0)
  const id = Buffer.alloc(4)
  id.writeUInt32LE(msgId, 0)
  return Buffer.concat([len, id, body])
}

function wsClientFrame(opcode, payload) {
  const len = payload.length
  let header
  if (len < 126) {
    header = Buffer.alloc(2)
    header[0] = 0x80 | opcode
    header[1] = 0x80 | len
  } else {
    header = Buffer.alloc(4)
    header[0] = 0x80 | opcode
    header[1] = 0x80 | 126
    header.writeUInt16BE(len, 2)
  }
  const mask = Buffer.from([1, 2, 3, 4])
  const masked = Buffer.alloc(payload.length)
  for (let i = 0; i < payload.length; i++) masked[i] = payload[i] ^ mask[i % 4]
  return Buffer.concat([header, mask, masked])
}

async function main() {
  const health = await fetch(`${API}/health`).then((r) => r.json())
  if (health.code !== 0 || !health.data.tls) throw new Error('https health tls!=true: ' + JSON.stringify(health))
  console.log('HTTPS /health ok tls=', health.data.tls)

  const login = await fetch(`${API}/api/v1/auth/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ device_id: 'tls-smoke' }),
  }).then((r) => r.json())
  if (login.code !== 0) throw new Error('login ' + JSON.stringify(login))
  const token = login.data.access_token

  await new Promise((resolve, reject) => {
    const sock = tls.connect({ host: WS_HOST, port: WS_PORT, servername: 'localhost', rejectUnauthorized: false }, () => {
      const key = 'dGhlIHNhbXBsZSBub25jZQ=='
      sock.write(
        `GET / HTTP/1.1\r\nHost: ${WS_HOST}:${WS_PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
          `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
      )
    })
    let buf = Buffer.alloc(0)
    let upgraded = false
    sock.on('data', (chunk) => {
      buf = Buffer.concat([buf, chunk])
      if (!upgraded) {
        const s = buf.toString('utf8')
        if (s.includes('\r\n\r\n')) {
          if (!s.includes('101')) {
            reject(new Error('wss upgrade fail: ' + s.slice(0, 200)))
            sock.destroy()
            return
          }
          upgraded = true
          sock.write(wsClientFrame(0x2, encodeFrame(1, encodeStringField(1, token))))
        }
      } else if (buf.length >= 2) {
        console.log('WSS auth response bytes=', buf.length)
        sock.end()
        resolve()
      }
    })
    sock.on('error', reject)
    setTimeout(() => reject(new Error('timeout')), 8000)
  })

  console.log('TLS SMOKE PASS')
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
