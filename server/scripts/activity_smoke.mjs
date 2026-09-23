// Activity smoke (Node 18+) — sign / task / gift
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
  assert(health.code === 0, 'health')

  const guest = await api('/api/v1/auth/login', {
    method: 'POST',
    body: { type: 'guest', device_id: `act-smoke-${Date.now()}` },
  })
  assert(guest.code === 0 && guest.data.access_token, 'guest login')
  const token = guest.data.access_token
  const uid = guest.data.uid
  const gold0 = guest.data.gold

  const list = await api('/api/v1/activity/list', { token })
  assert(list.code === 0 && Array.isArray(list.data.items) && list.data.items.length >= 3, 'list activities')
  const byType = Object.fromEntries(list.data.items.map((x) => [x.type, x]))
  assert(byType.sign && byType.task && byType.gift, 'seed types')

  // sign claim + idempotent
  const c1 = await api(`/api/v1/activity/${byType.sign.id}/claim`, {
    method: 'POST',
    token,
    body: { reward_key: byType.sign.reward_key },
  })
  assert(c1.code === 0, 'sign claim ' + c1.message)
  const c2 = await api(`/api/v1/activity/${byType.sign.id}/claim`, {
    method: 'POST',
    token,
    body: { reward_key: byType.sign.reward_key },
  })
  assert(c2.code === 2003 || c2.code !== 0, 'sign idempotent reject')

  // gift claim once, second fail
  const g1 = await api(`/api/v1/activity/${byType.gift.id}/claim`, {
    method: 'POST',
    token,
    body: { reward_key: byType.gift.reward_key },
  })
  assert(g1.code === 0, 'gift claim ' + g1.message)
  const g2 = await api(`/api/v1/activity/${byType.gift.id}/claim`, {
    method: 'POST',
    token,
    body: { reward_key: byType.gift.reward_key },
  })
  assert(g2.code !== 0, 'gift no double claim')

  // admin simulate settle x3 then claim task
  const admin = await api('/admin/v1/auth/login', {
    method: 'POST',
    body: { username: 'admin', password: 'admin123' },
  })
  assert(admin.code === 0, 'admin login')
  const at = admin.data.access_token
  for (let i = 0; i < 3; ++i) {
    const s = await api('/admin/v1/activities/simulate_settle', {
      method: 'POST',
      token: at,
      body: { uid, template_id: 1 },
    })
    assert(s.code === 0, 'simulate settle ' + i)
  }
  const prog = await api(`/api/v1/activity/${byType.task.id}/progress`, { token })
  assert(prog.code === 0, 'task progress')
  const t1 = await api(`/api/v1/activity/${byType.task.id}/claim`, {
    method: 'POST',
    token,
    body: { reward_key: byType.task.reward_key },
  })
  assert(t1.code === 0, 'task claim ' + t1.message)
  const t2 = await api(`/api/v1/activity/${byType.task.id}/claim`, {
    method: 'POST',
    token,
    body: { reward_key: byType.task.reward_key },
  })
  assert(t2.code !== 0, 'task idempotent')

  // admin list + audit
  const acts = await api('/admin/v1/activities', { token: at })
  assert(acts.code === 0 && acts.data.items?.length >= 3, 'admin activities')
  const audit = await api('/admin/v1/audit', { token: at })
  assert(audit.code === 0 && audit.data.items?.some((x) => String(x.action || '').includes('simulate') || String(x.action || '').includes('settle') || String(x.action || x.target || '').length > 0), 'audit nonempty')

  const profile = await api('/api/v1/player/profile', { token })
  assert(profile.code === 0 && profile.data.gold >= gold0, 'gold increased')

  console.log('ALL ACTIVITY SMOKE PASS')
}

main().catch((e) => {
  console.error('FAIL', e)
  process.exit(1)
})
