// Bag smoke (Node 18+) — FR-BAG grant/list/use/mail/admin
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

  const login = await api('/api/v1/auth/login', {
    method: 'POST',
    body: { type: 'guest', device_id: `bag-${Date.now()}` },
  })
  assert(login.code === 0, 'guest login')
  const token = login.data.access_token
  const uid = login.data.uid

  const admin = await api('/admin/v1/auth/login', {
    method: 'POST',
    body: { username: 'admin', password: 'admin123' },
  })
  assert(admin.code === 0, 'admin login')
  const at = admin.data.access_token

  const defs = await api('/admin/v1/items', { token: at })
  assert(defs.code === 0 && Array.isArray(defs.data.items) && defs.data.items.length >= 3, 'items list')

  const idemQty = `smoke_grant_qty:${uid}:${Date.now()}`
  const g1 = await api('/admin/v1/bag/grant', {
    method: 'POST',
    token: at,
    body: { uid, item_id: 1001, quantity: 2, idempotent_key: idemQty },
  })
  assert(g1.code === 0, 'grant qty ' + g1.message)
  const g1b = await api('/admin/v1/bag/grant', {
    method: 'POST',
    token: at,
    body: { uid, item_id: 1001, quantity: 2, idempotent_key: idemQty },
  })
  assert(g1b.code === 0, 'grant qty idempotent')

  const idemTtl = `smoke_grant_ttl:${uid}:${Date.now()}`
  const g2 = await api('/admin/v1/bag/grant', {
    method: 'POST',
    token: at,
    body: { uid, item_id: 2001, quantity: 1, expire_sec: 86400, idempotent_key: idemTtl },
  })
  assert(g2.code === 0, 'grant ttl ' + g2.message)
  const exp1 = g2.data.expire_at
  assert(!!exp1, 'ttl expire_at')

  // ttl 续期：再发一份，到期应延后（同 item 仍一行）
  const g2b = await api('/admin/v1/bag/grant', {
    method: 'POST',
    token: at,
    body: {
      uid,
      item_id: 2001,
      quantity: 1,
      expire_sec: 86400,
      idempotent_key: `smoke_grant_ttl_renew:${uid}:${Date.now()}`,
    },
  })
  assert(g2b.code === 0, 'grant ttl renew ' + g2b.message)
  assert(g2b.data.expire_at && g2b.data.expire_at > exp1, `ttl renew extend ${exp1} -> ${g2b.data.expire_at}`)
  const bagTtl = await api('/api/v1/bag/list', { token })
  const ttlRows = (bagTtl.data.items || []).filter((x) => x.item_id === 2001)
  assert(ttlRows.length === 1, 'ttl single row')

  const idemQtyTtl = `smoke_grant_qtyttl:${uid}:${Date.now()}`
  const g3 = await api('/admin/v1/bag/grant', {
    method: 'POST',
    token: at,
    body: { uid, item_id: 1002, quantity: 3, expire_sec: 3600, idempotent_key: idemQtyTtl },
  })
  assert(g3.code === 0, 'grant qty_ttl ' + g3.message)

  const bag = await api('/api/v1/bag/list', { token })
  assert(bag.code === 0 && Array.isArray(bag.data.items), 'bag list')
  const has1001 = bag.data.items.some((x) => x.item_id === 1001 && x.quantity >= 2)
  const has2001 = bag.data.items.some((x) => x.item_id === 2001 && x.quantity >= 1)
  const has1002 = bag.data.items.some((x) => x.item_id === 1002 && x.quantity >= 3)
  assert(has1001 && has2001 && has1002, 'bag has granted items')

  const qtyBefore = bag.data.items.find((x) => x.item_id === 1001).quantity
  const use1 = await api('/api/v1/bag/use', {
    method: 'POST',
    token,
    body: { item_id: 1001, quantity: 1, idempotent_key: `smoke_use:${uid}:${Date.now()}` },
  })
  assert(use1.code === 0 && use1.data.quantity_after === qtyBefore - 1, 'bag use ' + use1.message)

  const adminBag = await api(`/admin/v1/players/${uid}/bag`, { token: at })
  assert(adminBag.code === 0 && adminBag.data.items.some((x) => x.item_id === 1001), 'admin player bag')

  const ledgers = await api(`/admin/v1/item/ledgers?uid=${uid}&page=1&page_size=20`, { token: at })
  assert(ledgers.code === 0 && ledgers.data.total >= 1, 'item ledgers')

  const send = await api('/admin/v1/mail/send', {
    method: 'POST',
    token: at,
    body: {
      scope: 'uids',
      uids: [uid],
      title: 'bag item mail',
      body: 'gift item',
      attach_json: { items: [{ item_id: 1001, quantity: 1, expire_sec: 0 }] },
    },
  })
  assert(send.code === 0, 'mail with items ' + send.message)
  const mails = await api('/api/v1/mail/list', { token })
  assert(mails.code === 0 && mails.data.items.length >= 1, 'mail list')
  const mid = mails.data.items.find((m) => m.title === 'bag item mail')?.id || mails.data.items[0].id
  const claim = await api(`/api/v1/mail/${mid}/claim`, { method: 'POST', token })
  assert(claim.code === 0, 'mail claim items ' + claim.message)

  const bag2 = await api('/api/v1/bag/list', { token })
  assert(bag2.code === 0 && bag2.data.items.some((x) => x.item_id === 1001 && x.quantity >= qtyBefore), 'qty after mail')

  const revoke = await api('/admin/v1/bag/revoke', {
    method: 'POST',
    token: at,
    body: {
      uid,
      item_id: 1002,
      quantity: 1,
      idempotent_key: `smoke_revoke:${uid}:${Date.now()}`,
    },
  })
  assert(revoke.code === 0, 'revoke ' + revoke.message)

  // pay gift items
  const upsert = await api('/admin/v1/pay/products', {
    method: 'POST',
    token: at,
    body: {
      id: 1,
      amount_fen: 600,
      diamond: 60,
      gift_diamond: 0,
      gift_items: [{ item_id: 1001, quantity: 1, expire_sec: 0 }],
      enabled: true,
    },
  })
  assert(upsert.code === 0, 'upsert product gift ' + upsert.message)
  const bagBeforePay = await api('/api/v1/bag/list', { token })
  const qtyPay0 = bagBeforePay.data.items.find((x) => x.item_id === 1001)?.quantity || 0
  const created = await api('/api/v1/pay/alipay/create', {
    method: 'POST',
    token,
    body: { product_id: 1 },
  })
  assert(created.code === 0, 'pay create ' + created.message)
  const paid = await api('/api/v1/pay/alipay/sandbox_complete', {
    method: 'POST',
    token,
    body: { order_id: created.data.order_id },
  })
  assert(paid.code === 0, 'sandbox complete ' + paid.message)
  const bagAfterPay = await api('/api/v1/bag/list', { token })
  const qtyPay1 = bagAfterPay.data.items.find((x) => x.item_id === 1001)?.quantity || 0
  assert(qtyPay1 >= qtyPay0 + 1, 'pay gift item granted')

  console.log('bag_smoke OK uid=', uid)
}

main().catch((e) => {
  console.error('bag_smoke FAIL', e)
  process.exit(1)
})
