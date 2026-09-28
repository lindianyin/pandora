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
  gift_items?: Array<{ item_id: number; quantity: number; expire_sec?: number }>
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

export type RecordSummary = {
  total_rounds: number
  win_rounds: number
  lose_rounds: number
  win_rate_bp: number
  landlord_rounds: number
  gold_win_sum: number
  gold_lose_sum: number
}

export type RecentRound = {
  round_id: number
  template_id: number
  ended_at: string
  base_score: number
  multiplier: number
  delta_gold: number
  is_landlord: boolean
  result: string
}

export async function fetchRecordSummary(token: string) {
  const res = await fetch(`${API_BASE}/api/v1/record/summary`, { headers: authHeaders(token) })
  return (await res.json()) as ApiResult<RecordSummary>
}

export async function fetchRecordRecent(token: string, page = 1, pageSize = 20) {
  const res = await fetch(`${API_BASE}/api/v1/record/recent?page=${page}&page_size=${pageSize}`, {
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{ items: RecentRound[]; total: number }>
}

export type FriendItem = { uid: number; nickname: string; online: boolean }

export async function fetchFriends(token: string) {
  const res = await fetch(`${API_BASE}/api/v1/friend/list`, { headers: authHeaders(token) })
  return (await res.json()) as ApiResult<{
    items: FriendItem[]
    pending?: { incoming: Array<{ id: number; from_uid: number; nickname: string }>; outgoing: unknown[] }
  }>
}

export async function friendRequest(token: string, toUid: number) {
  const res = await fetch(`${API_BASE}/api/v1/friend/request`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ to_uid: toUid }),
  })
  return (await res.json()) as ApiResult<{ ok: boolean }>
}

export async function friendAccept(token: string, fromUid: number) {
  const res = await fetch(`${API_BASE}/api/v1/friend/accept`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ from_uid: fromUid }),
  })
  return (await res.json()) as ApiResult<{ ok: boolean }>
}

export async function friendReject(token: string, fromUid: number) {
  const res = await fetch(`${API_BASE}/api/v1/friend/reject`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ from_uid: fromUid }),
  })
  return (await res.json()) as ApiResult<{ ok: boolean }>
}

export async function friendRemove(token: string, friendUid: number) {
  const res = await fetch(`${API_BASE}/api/v1/friend/remove`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify({ friend_uid: friendUid }),
  })
  return (await res.json()) as ApiResult<{ ok: boolean }>
}

export type MailItem = {
  id: number
  title: string
  body: string
  attach_json: {
    currency?: number
    amount?: number
    items?: Array<{ item_id: number; quantity: number; expire_sec?: number }>
  }
  status: number
  expire_at: string
  created_at: string
}

export async function fetchMails(token: string) {
  const res = await fetch(`${API_BASE}/api/v1/mail/list`, { headers: authHeaders(token) })
  return (await res.json()) as ApiResult<{ items: MailItem[] }>
}

export async function mailRead(token: string, id: number) {
  const res = await fetch(`${API_BASE}/api/v1/mail/${id}/read`, {
    method: 'POST',
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{ ok: boolean }>
}

export async function mailClaim(token: string, id: number) {
  const res = await fetch(`${API_BASE}/api/v1/mail/${id}/claim`, {
    method: 'POST',
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{ balance: number; currency: number }>
}

export async function mailDelete(token: string, id: number) {
  const res = await fetch(`${API_BASE}/api/v1/mail/${id}/delete`, {
    method: 'POST',
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{ ok: boolean }>
}

export type RankEntry = { rank: number; uid: number; nickname: string; score: number }

export async function fetchRank(token: string, period: 'daily' | 'weekly', limit = 50) {
  const res = await fetch(`${API_BASE}/api/v1/rank/${period}?limit=${limit}`, {
    headers: authHeaders(token),
  })
  return (await res.json()) as ApiResult<{
    period: string
    period_key: string
    list: RankEntry[]
    me: { rank: number; score: number }
  }>
}

export type BagItem = {
  item_id: number
  name: string
  kind: string
  quantity: number
  expire_at: string
  icon?: string
  tag?: string
}

export async function fetchBag(token: string, includeExpired = false) {
  const q = includeExpired ? '?include_expired=1' : ''
  const res = await fetch(`${API_BASE}/api/v1/bag/list${q}`, { headers: authHeaders(token) })
  return (await res.json()) as ApiResult<{ items: BagItem[] }>
}

export async function useBagItem(
  token: string,
  body: { item_id: number; quantity: number; expire_at?: string; idempotent_key: string },
) {
  const res = await fetch(`${API_BASE}/api/v1/bag/use`, {
    method: 'POST',
    headers: authHeaders(token),
    body: JSON.stringify(body),
  })
  return (await res.json()) as ApiResult<{ ok: boolean; quantity_after: number }>
}

