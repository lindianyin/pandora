// Social smoke (Node 18+) — record / friend / mail / rank
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

async function sleep(ms) {
  await new Promise((r) => setTimeout(r, ms))
}

async function main() {
  const health = await api('/health')
  assert(health.code === 0, 'health')

  const a = await api('/api/v1/auth/login', {
    method: 'POST',
    body: { type: 'guest', device_id: `soc-a-${Date.now()}` },
  })
  const b = await api('/api/v1/auth/login', {
    method: 'POST',
    body: { type: 'guest', device_id: `soc-b-${Date.now()}` },
  })
  assert(a.code === 0 && b.code === 0, 'guest login')
  const ta = a.data.access_token
  const tb = b.data.access_token
  const ua = a.data.uid
  const ub = b.data.uid
  const gold0 = a.data.gold

  const admin = await api('/admin/v1/auth/login', {
    method: 'POST',
    body: { username: 'admin', password: 'admin123' },
  })
  assert(admin.code === 0, 'admin login')
  const at = admin.data.access_token

  // FR-SOC-01 record
  const sim = await api('/admin/v1/social/simulate_round', {
    method: 'POST',
    token: at,
    body: { uid: ua, delta: 500 },
  })
  assert(sim.code === 0, 'simulate round ' + sim.message)
  await sleep(300)
  const summary = await api('/api/v1/record/summary', { token: ta })
  assert(summary.code === 0 && summary.data.total_rounds >= 1, 'summary')
  const recent = await api('/api/v1/record/recent', { token: ta })
  assert(recent.code === 0 && Array.isArray(recent.data.items) && recent.data.items.length >= 1, 'recent')

  // FR-SOC-02 friends
  const req = await api('/api/v1/friend/request', { method: 'POST', token: ta, body: { to_uid: ub } })
  assert(req.code === 0, 'friend request ' + req.message)
  const acc = await api('/api/v1/friend/accept', { method: 'POST', token: tb, body: { from_uid: ua } })
  assert(acc.code === 0, 'friend accept ' + acc.message)
  const listA = await api('/api/v1/friend/list', { token: ta })
  assert(listA.code === 0 && listA.data.items.some((x) => x.uid === ub), 'friend list')
  const rem = await api('/api/v1/friend/remove', { method: 'POST', token: ta, body: { friend_uid: ub } })
  assert(rem.code === 0, 'friend remove')

  // FR-SOC-03 mail
  const send = await api('/admin/v1/mail/send', {
    method: 'POST',
    token: at,
    body: {
      scope: 'uids',
      uids: [ua],
      title: 'smoke reward',
      body: 'hello',
      attach_json: { currency: 1, amount: 100 },
    },
  })
  assert(send.code === 0, 'mail send ' + send.message)
  const mails = await api('/api/v1/mail/list', { token: ta })
  assert(mails.code === 0 && mails.data.items.length >= 1, 'mail list')
  const mid = mails.data.items[0].id
  const claim1 = await api(`/api/v1/mail/${mid}/claim`, { method: 'POST', token: ta })
  assert(claim1.code === 0, 'mail claim ' + claim1.message)
  const claim2 = await api(`/api/v1/mail/${mid}/claim`, { method: 'POST', token: ta })
  assert(claim2.code !== 0, 'mail claim idempotent')
  const profile = await api('/api/v1/player/profile', { token: ta })
  assert(profile.code === 0 && profile.data.gold >= gold0 + 100, 'gold after mail')

  // FR-SOC-04 rank
  await sleep(200)
  const rank = await api('/api/v1/rank/daily?limit=20', { token: ta })
  assert(rank.code === 0 && Array.isArray(rank.data.list), 'rank daily')
  const snap = await api('/admin/v1/rank/snapshot', {
    method: 'POST',
    token: at,
    body: { period: 'daily' },
  })
  assert(snap.code === 0, 'rank snapshot ' + snap.message)
  const snapGet = await api('/admin/v1/rank/snapshot?period=daily', { token: at })
  assert(snapGet.code === 0 && Array.isArray(snapGet.data.items), 'rank snapshot get')

  console.log('social_smoke OK', { ua, ub, gold: profile.data.gold, rank_me: rank.data.me })
}

main().catch((e) => {
  console.error('social_smoke FAIL', e)
  process.exit(1)
})
