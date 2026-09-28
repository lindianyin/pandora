import { qs } from './utils'

const API = import.meta.env.VITE_ADMIN_API || 'https://127.0.0.1:8443'

export type ApiResult<T> = { code: number; message: string; data: T; trace_id: string }

export type PageQuery = { page?: number; page_size?: number; q?: string; uid?: number; status?: number }

function token() {
  return localStorage.getItem('admin_token') || ''
}

async function req<T>(method: string, path: string, body?: unknown): Promise<ApiResult<T>> {
  const res = await fetch(`${API}${path}`, {
    method,
    headers: {
      'Content-Type': 'application/json',
      ...(token() ? { Authorization: `Bearer ${token()}` } : {}),
    },
    body: body ? JSON.stringify(body) : undefined,
  })
  return (await res.json()) as ApiResult<T>
}

export const api = {
  login: (username: string, password: string) =>
    req<{ access_token: string; username: string; role: string }>('POST', '/admin/v1/auth/login', {
      username,
      password,
    }),
  dashboard: () => req<Record<string, unknown>>('GET', '/admin/v1/dashboard'),
  players: (p: PageQuery = {}) =>
    req<{ total: number; items: any[] }>('GET', `/admin/v1/players${qs({ page: p.page, page_size: p.page_size, q: p.q })}`),
  kick: (uid: number) => req('POST', `/admin/v1/players/${uid}/kick`),
  ban: (uid: number) => req('POST', `/admin/v1/players/${uid}/ban`),
  unban: (uid: number) => req('POST', `/admin/v1/players/${uid}/unban`),
  adjust: (body: { uid: number; currency: number; delta: number; idempotent_key: string }) =>
    req<{ balance: number }>('POST', '/admin/v1/wallet/adjust', body),
  ledgers: (p: PageQuery = {}) =>
    req<{ total: number; items: any[] }>(
      'GET',
      `/admin/v1/wallet/ledgers${qs({ page: p.page, page_size: p.page_size, uid: p.uid })}`,
    ),
  rounds: (p: PageQuery = {}) =>
    req<{ total: number; items: any[] }>(
      'GET',
      `/admin/v1/rounds${qs({ page: p.page, page_size: p.page_size, uid: p.uid })}`,
    ),
  templates: () => req<{ items: any[] }>('GET', '/admin/v1/rooms/templates'),
  putTemplate: (body: any) => req('PUT', '/admin/v1/rooms/templates', body),
  products: () => req<{ items: any[] }>('GET', '/admin/v1/pay/products'),
  upsertProduct: (body: any) => req('POST', '/admin/v1/pay/products', body),
  setProductEnabled: (id: number, enabled: boolean) =>
    req('POST', `/admin/v1/pay/products/${id}/enable`, { enabled }),
  deleteProduct: (id: number) => req('DELETE', `/admin/v1/pay/products/${id}`),
  orders: (p: PageQuery = {}) =>
    req<{ total: number; items: any[] }>(
      'GET',
      `/admin/v1/pay/orders${qs({ page: p.page, page_size: p.page_size, uid: p.uid, status: p.status })}`,
    ),
  announce: (message: string) => req('POST', '/admin/v1/announce', { message }),
  sendMail: (body: {
    scope: 'all' | 'uids'
    uids?: number[]
    title: string
    body: string
    attach_json?: { currency: number; amount: number } | Record<string, never>
  }) => req<{ ok: boolean }>('POST', '/admin/v1/mail/send', body),
  mailLogs: (p: PageQuery = {}) =>
    req<{ total: number; items: any[] }>(
      'GET',
      `/admin/v1/mail${qs({ page: p.page, page_size: p.page_size })}`,
    ),
  maintain: (enabled: boolean) => req('POST', '/admin/v1/ops/maintain', { enabled }),
  audit: (p: PageQuery = {}) =>
    req<{ total: number; items: any[] }>(
      'GET',
      `/admin/v1/audit${qs({ page: p.page, page_size: p.page_size, q: p.q })}`,
    ),
  activities: (p: { q?: string } = {}) =>
    req<{ items: any[]; total?: number }>('GET', `/admin/v1/activities${qs({ q: p.q })}`),
  upsertActivity: (body: any) => req('POST', '/admin/v1/activities', body),
  setActivityEnabled: (id: number, enabled: boolean) =>
    req('POST', `/admin/v1/activities/${id}/enable`, { enabled }),
  deleteActivity: (id: number) => req('DELETE', `/admin/v1/activities/${id}`),
  simulateSettle: (uid: number, template_id = 1) =>
    req('POST', '/admin/v1/activities/simulate_settle', { uid, template_id }),
  exportReport: async (kind: 'ledgers' | 'rounds' | 'claims', limit = 5000) => {
    const API = import.meta.env.VITE_ADMIN_API || 'https://127.0.0.1:8443'
    const token = localStorage.getItem('admin_token') || ''
    const res = await fetch(`${API}/admin/v1/reports/${kind}.csv?limit=${limit}`, {
      headers: token ? { Authorization: `Bearer ${token}` } : {},
    })
    if (!res.ok) throw new Error(`export failed ${res.status}`)
    return res.blob()
  },
}
