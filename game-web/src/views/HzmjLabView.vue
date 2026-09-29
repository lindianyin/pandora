<script setup lang="ts">
import { ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import HzmjLabPanel from './HzmjLabPanel.vue'

const router = useRouter()
const route = useRoute()
const booting = ref(false)
const panels = ref<InstanceType<typeof HzmjLabPanel>[]>([])

function queryOne(v: unknown): string {
  const s = Array.isArray(v) ? v[0] : v
  return typeof s === 'string' ? s.trim().slice(0, 128) : ''
}

/** d0–d3, or hw=a,b,c,d. Missing slots are filled and written back so refresh keeps the same guests. */
function labDeviceIds(): string[] {
  const q = route.query
  const packed = queryOne(q.hw || q.devices)
    .split(/[,|]/)
    .map((s) => s.trim())
    .filter(Boolean)
  const ids = [0, 1, 2, 3].map((i) => queryOne(q[`d${i}`] ?? q[`hw${i}`]) || packed[i] || '')
  const missing = ids.some((s) => !s)
  for (let i = 0; i < 4; i++) {
    if (!ids[i]) ids[i] = `lab${i}-${Math.random().toString(16).slice(2, 10)}`
  }
  if (missing) {
    const query: Record<string, string> = {}
    for (const [k, v] of Object.entries(q)) {
      const one = queryOne(v)
      if (one && !/^d[0-3]$/.test(k) && k !== 'hw' && k !== 'devices' && !/^hw[0-3]$/.test(k)) query[k] = one
    }
    ids.forEach((id, i) => {
      query[`d${i}`] = id
    })
    router.replace({ path: '/hzmj-lab', query })
  }
  return ids
}

const devices = labDeviceIds()

function setPanel(el: unknown, i: number) {
  if (el) panels.value[i] = el as InstanceType<typeof HzmjLabPanel>
}

async function bootAll() {
  booting.value = true
  try {
    for (let i = 0; i < 4; ++i) {
      const p = panels.value[i]
      if (p && !p.wsOk) await p.login()
      await sleep(200)
    }
  } finally {
    booting.value = false
  }
}

async function matchAll() {
  for (let i = 0; i < 4; ++i) {
    const p = panels.value[i]
    if (p && p.wsOk && !p.roomId) p.matchHzmj()
    await sleep(80)
  }
}

async function readyAll() {
  for (let i = 0; i < 4; ++i) {
    const p = panels.value[i]
    if (p && p.roomId) p.doReady()
    await sleep(50)
  }
}

function sleep(ms: number) {
  return new Promise((r) => setTimeout(r, ms))
}
</script>

<template>
  <div class="lab">
    <header class="bar">
      <div>
        <h1>杭州麻将 · 四联调试</h1>
        <p class="tip">硬件码取自 URL 的 d0–d3。刷新后再登录，同一码回到原账号，用于断线重连。</p>
      </div>
      <div class="ops">
        <button :disabled="booting" @click="bootAll">一键登录四人</button>
        <button @click="matchAll">全员匹配</button>
        <button @click="readyAll">全员准备</button>
        <button class="ghost" @click="router.push('/lobby')">返回大厅</button>
      </div>
    </header>
    <div class="grid">
      <HzmjLabPanel
        v-for="i in 4"
        :key="i"
        :slot-index="i - 1"
        :device-id="devices[i - 1] || ''"
        :ref="(el) => setPanel(el, i - 1)"
      />
    </div>
  </div>
</template>

<style scoped>
.lab { min-height: 100vh; }
.bar {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.bar h1 { margin: 0; font-size: 1.25rem; }
.tip { margin: 4px 0 0; color: #64748b; font-size: 13px; }
.ops { display: flex; gap: 8px; flex-wrap: wrap; }
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  grid-template-areas:
    "s3 s2"
    "s0 s1";
  gap: 12px;
  position: relative;
}
:deep(.pos-s0) { grid-area: s0; }
:deep(.pos-s1) { grid-area: s1; }
:deep(.pos-s2) { grid-area: s2; }
:deep(.pos-s3) { grid-area: s3; }
@media (max-width: 1100px) {
  .grid {
    grid-template-columns: 1fr;
    grid-template-areas:
      "s2"
      "s3"
      "s1"
      "s0";
  }
}
button {
  border: 0;
  background: #0f766e;
  color: #fff;
  padding: 8px 12px;
  border-radius: 6px;
  cursor: pointer;
}
button:disabled { opacity: 0.5; }
button.ghost { background: #e2e8f0; color: #334155; }
</style>
