const API = import.meta.env.VITE_ADMIN_API || 'https://127.0.0.1:8443'

export type ApiResult<T> = { code: number; message: string; data: T; trace_id: string }

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
  players: () => req<{ total: number; items: any[] }>('GET', '/admin/v1/players'),
  kick: (uid: number) => req('POST', `/admin/v1/players/${uid}/kick`),
  ban: (uid: number) => req('POST', `/admin/v1/players/${uid}/ban`),
  unban: (uid: number) => req('POST', `/admin/v1/players/${uid}/unban`),
  adjust: (body: { uid: number; currency: number; delta: number; idempotent_key: string }) =>
    req<{ balance: number }>('POST', '/admin/v1/wallet/adjust', body),
  ledgers: () => req<{ total: number; items: any[] }>('GET', '/admin/v1/wallet/ledgers'),
  rounds: () => req<{ total: number; items: any[] }>('GET', '/admin/v1/rounds'),
  templates: () => req<{ items: any[] }>('GET', '/admin/v1/rooms/templates'),
  putTemplate: (body: any) => req('PUT', '/admin/v1/rooms/templates', body),
  products: () => req<{ items: any[] }>('GET', '/admin/v1/pay/products'),
  upsertProduct: (body: any) => req('POST', '/admin/v1/pay/products', body),
  deleteProduct: (id: number) => req('DELETE', `/admin/v1/pay/products/${id}`),
  orders: () => req<{ total: number; items: any[] }>('GET', '/admin/v1/pay/orders'),
  announce: (message: string) => req('POST', '/admin/v1/announce', { message }),
  maintain: (enabled: boolean) => req('POST', '/admin/v1/ops/maintain', { enabled }),
  audit: () => req<{ total: number; items: any[] }>('GET', '/admin/v1/audit'),
  activities: () => req<{ items: any[] }>('GET', '/admin/v1/activities'),
  upsertActivity: (body: any) => req('POST', '/admin/v1/activities', body),
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

