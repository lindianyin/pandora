<script setup lang="ts">
import { ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useGameSession } from '../composables/useGameSession'

const route = useRoute()
const router = useRouter()
const { busy, errorBanner, loginAndConnect, checkHealth, logs } = useGameSession()

function queryOne(v: unknown): string {
  const s = Array.isArray(v) ? v[0] : v
  return typeof s === 'string' ? s.trim().slice(0, 128) : ''
}

/** Prefer URL d / device / hw / d0 (same spirit as /hzmj-lab). */
function resolveDeviceId(): string {
  const fromUrl = queryOne(route.query.d || route.query.device || route.query.hw || route.query.d0)
  if (fromUrl) return fromUrl
  const saved = sessionStorage.getItem('pandora_device') || ''
  if (saved) return saved
  return `web-${Math.random().toString(16).slice(2, 10)}`
}

const deviceId = ref(resolveDeviceId())

if (!queryOne(route.query.d || route.query.device || route.query.hw || route.query.d0)) {
  const query: Record<string, string> = { d: deviceId.value }
  for (const [k, v] of Object.entries(route.query)) {
    const one = queryOne(v)
    if (one && k !== 'd' && k !== 'device' && k !== 'hw' && k !== 'd0') query[k] = one
  }
  router.replace({ path: '/login', query })
}

function doLogin() {
  const id = deviceId.value.trim().slice(0, 128)
  if (!id) {
    return
  }
  deviceId.value = id
  sessionStorage.setItem('pandora_device', id)
  if (queryOne(route.query.d) !== id) {
    const query: Record<string, string> = { d: id }
    for (const [k, v] of Object.entries(route.query)) {
      const one = queryOne(v)
      if (one && k !== 'd' && k !== 'device' && k !== 'hw' && k !== 'd0') query[k] = one
    }
    router.replace({ path: '/login', query })
  }
  return loginAndConnect(id)
}
</script>

<template>
  <section class="card">
    <h2>登录</h2>
    <p class="hint-text">
      游客登录后自动连接 WSS。硬件码可用 URL
      <code>?d=xxx</code>
      指定（与四联
      <code>d0–d3</code>
      相同）；同码回到同一账号，便于断线重连。
    </p>
    <label class="field">
      <span>硬件码</span>
      <input v-model="deviceId" type="text" maxlength="128" placeholder="device_id" :disabled="busy" />
    </label>
    <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>
    <div class="row">
      <button :disabled="busy || !deviceId.trim()" @click="doLogin">游客登录并连接</button>
      <button @click="checkHealth">检查 /health</button>
      <button class="ghost" @click="$router.push('/hzmj-lab')">麻将四联调试</button>
      <button class="ghost" @click="$router.push('/ddz-lab')">斗地主三联调试</button>
      <button class="ghost" @click="$router.push('/phz-lab')">跑胡子三联调试</button>
    </div>
  </section>

  <section class="card">
    <h2>日志</h2>
    <pre class="log">{{ logs.join('\n') }}</pre>
  </section>
</template>

<style scoped>
.hint-text { color: #64748b; margin: 0 0 12px; font-size: 14px; }
.hint-text code {
  font-family: ui-monospace, Consolas, monospace;
  background: #f1f5f9;
  padding: 1px 5px;
  border-radius: 4px;
  font-size: 12px;
}
.field {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-bottom: 12px;
  font-size: 13px;
  color: #475569;
}
.field input {
  border: 1px solid #cbd5e1;
  border-radius: 6px;
  padding: 8px 10px;
  font-family: ui-monospace, Consolas, monospace;
  font-size: 13px;
}
.banner {
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 12px;
}
.banner.err { background: #fef2f2; color: #b91c1c; }
.row { display: flex; gap: 8px; flex-wrap: wrap; }
button {
  border: 0;
  background: #2563eb;
  color: #fff;
  padding: 8px 14px;
  border-radius: 6px;
  cursor: pointer;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.ghost { background: #e2e8f0; color: #334155; }
.card {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 16px;
  margin-bottom: 16px;
}
.log {
  margin: 0;
  max-height: 240px;
  overflow: auto;
  background: #0f172a;
  color: #e2e8f0;
  padding: 12px;
  border-radius: 8px;
  font-size: 12px;
}
</style>
