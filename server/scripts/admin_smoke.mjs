// Admin API smoke (Node 18+)
const API = 'http://127.0.0.1:8080'

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

function assert(cond, msg) {
  if (!cond) throw new Error(msg)
}

async function main() {
  const health = await api('/health')
  assert(health.code === 0 && health.data.mysql === true && health.data.redis === true, 'health deps')

  const login = await api('/admin/v1/auth/login', {
    method: 'POST',
    body: { username: 'admin', password: 'admin123' },
  })
  assert(login.code === 0 && login.data.access_token, 'admin login')
  const token = login.data.access_token

  // create a player for adjust
  const guest = await api('/api/v1/auth/login', {
    method: 'POST',
    body: { type: 'guest', device_id: `adm-smoke-${Date.now()}` },
  })
  assert(guest.code === 0, 'guest login')
  const uid = guest.data.uid

  const idem = `smoke-${Date.now()}`
  const adj1 = await api('/admin/v1/wallet/adjust', {
    method: 'POST',
    token,
    body: { uid, currency: 1, delta: 50, idempotent_key: idem },
  })
  assert(adj1.code === 0, 'adjust1 ' + adj1.message)
  const bal = adj1.data.balance
  const adj2 = await api('/admin/v1/wallet/adjust', {
    method: 'POST',
    token,
    body: { uid, currency: 1, delta: 50, idempotent_key: idem },
  })
  assert(adj2.code === 0 && adj2.data.balance === bal, 'adjust idempotent')

  const put = await api('/admin/v1/rooms/templates', {
    method: 'PUT',
    token,
    body: {
      id: 1,
      game_id: 2000,
      name: '初级场',
      base_score: 110,
      rake_bp: 500,
      min_gold: 1000,
      max_gold: 0,
      enabled: true,
    },
  })
  assert(put.code === 0, 'put template')

  const tpls = await api('/admin/v1/rooms/templates', { token })
  const t1 = tpls.data.items?.find((t) => t.id === 1)
  assert(tpls.code === 0 && t1?.base_score === 110 && t1?.game_id === 2000, 'template hot')

  const audit = await api('/admin/v1/audit', { token })
  assert(audit.code === 0 && audit.data.items?.length > 0, 'audit')

  // player token forbidden on admin
  const denied = await api('/admin/v1/dashboard', { token: guest.data.access_token })
  assert(denied.code === 10002, 'player token denied')

  console.log('ALL ADMIN SMOKE PASS')
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
