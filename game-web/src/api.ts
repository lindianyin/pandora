const API_BASE = import.meta.env.VITE_API_BASE || 'https://127.0.0.1:8443'

export type ApiResult<T> = { code: number; message: string; data: T; trace_id: string }

function authHeaders(token: string): HeadersInit {
  return {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${token}`,
  }
}

export async function loginGuest(deviceId: string) {
  const res = await fetch(`${API_BASE}/api/v1/auth/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ type: 'guest', device_id: deviceId }),
  })
  return (await res.json()) as ApiResult<{
    access_token: string
    uid: number
    nickname: string
    gold: number
    diamond: number
  }>
}

export async function health() {
  const res = await fetch(`${API_BASE}/health`)
  return res.json()
}

export async function fetchProfile(token: string) {
  const res = await fetch(`${API_BASE}/api/v1/player/profile`, {
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{
    uid: number
    nickname: string
    gold: number
    diamond: number
    level: number
  }>
}

export async function exchangeDiamond(token: string, diamond: number, clientOrderId: string) {
  const res = await fetch(`${API_BASE}/api/v1/wallet/exchange`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ diamond, client_order_id: clientOrderId }),
  })
  return (await res.json()) as ApiResult<{ gold: number; diamond?: number }>
}

export type PayProduct = {
  id: number
  amount_fen: number
  diamond: number
  gift_diamond: number
}

export async function fetchPayProducts(token: string) {
  const res = await fetch(`${API_BASE}/api/v1/pay/products`, {
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{ items: PayProduct[]; sandbox: boolean }>
}

export async function createAlipayOrder(token: string, productId: number) {
  const res = await fetch(`${API_BASE}/api/v1/pay/alipay/create`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ product_id: productId }),
  })
  return (await res.json()) as ApiResult<{
    order_id: string
    amount_fen: number
    diamond: number
    alipay_order_str: string
    sandbox: boolean
  }>
}

export async function sandboxComplete(token: string, orderId: string) {
  const res = await fetch(`${API_BASE}/api/v1/pay/alipay/sandbox_complete`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ order_id: orderId }),
  })
  return (await res.json()) as ApiResult<{ ok: boolean }>
}

export type ActivityItem = {
  id: number
  type: string
  title: string
  rules_json: Record<string, unknown>
  progress_json: Record<string, unknown>
  claimable: boolean
  claimed: boolean
  reward_key: string
}

export async function fetchActivities(token: string) {
  const res = await fetch(`${API_BASE}/api/v1/activity/list`, {
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{ items: ActivityItem[] }>
}

export async function claimActivity(token: string, activityId: number, rewardKey: string) {
  const res = await fetch(`${API_BASE}/api/v1/activity/${activityId}/claim`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ reward_key: rewardKey }),
  })
  return (await res.json()) as ApiResult<{ balance: number; currency: number }>
}

