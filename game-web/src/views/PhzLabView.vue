<script setup lang="ts">
import { computed, nextTick, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import PhzLabPanel from './PhzLabPanel.vue'

const router = useRouter()
const route = useRoute()
const booting = ref(false)
const panels = ref<InstanceType<typeof PhzLabPanel>[]>([])

function queryOne(v: unknown): string {
  const s = Array.isArray(v) ? v[0] : v
  return typeof s === 'string' ? s.trim().slice(0, 128) : ''
}

function labDeviceIds(): string[] {
  const q = route.query
  const packed = queryOne(q.hw || q.devices)
    .split(/[,|]/)
    .map((s) => s.trim())
    .filter(Boolean)
  const ids = [0, 1, 2].map((i) => queryOne(q[`d${i}`] ?? q[`hw${i}`]) || packed[i] || '')
  const missing = ids.some((s) => !s)
  for (let i = 0; i < 3; i++) {
    if (!ids[i]) ids[i] = `phzlab${i}-${Math.random().toString(16).slice(2, 10)}`
  }
  if (missing) {
    const query: Record<string, string> = {}
    for (const [k, v] of Object.entries(q)) {
      const one = queryOne(v)
      if (one && !/^d[0-2]$/.test(k) && k !== 'hw' && k !== 'devices' && !/^hw[0-2]$/.test(k)) query[k] = one
    }
    ids.forEach((id, i) => {
      query[`d${i}`] = id
    })
    router.replace({ path: '/phz-lab', query })
  }
  return ids
}

const devices = labDeviceIds()

function setPanel(el: unknown, i: number) {
  if (el) panels.value[i] = el as InstanceType<typeof PhzLabPanel>
}

const canLeaveAll = computed(() => panels.value.some((p) => p && (p.roomId || p.matching)))

async function bootAll() {
  booting.value = true
  try {
    for (let i = 0; i < 3; ++i) {
      const p = panels.value[i]
      if (p && !p.wsOk) await p.login()
      await sleep(200)
    }
  } finally {
    booting.value = false
  }
}

async function matchAll() {
  for (let i = 0; i < 3; ++i) {
    const p = panels.value[i]
    if (p && p.wsOk && !p.roomId) p.matchPhz()
    await sleep(80)
  }
}

async function readyAll() {
  for (let i = 0; i < 3; ++i) {
    const p = panels.value[i]
    if (p && p.roomId) p.doReady()
    await sleep(50)
  }
}

function leaveAll() {
  for (const p of panels.value) {
    if (p) p.leave()
  }
}

function backLobby() {
  leaveAll()
  router.push('/lobby')
}

function sleep(ms: number) {
  return new Promise((r) => setTimeout(r, ms))
}

onMounted(async () => {
  await nextTick()
  await bootAll()
})
</script>

<template>
  <div class="lab">
    <header class="bar">
      <div>
        <h1>跑胡子 · 三联调试</h1>
        <p class="tip">进入后自动登录三人；硬件码写入 d0–d2，刷新同一游客。</p>
      </div>
      <div class="actions">
        <button class="primary" :disabled="booting" @click="matchAll">一键匹配</button>
        <button class="primary" @click="readyAll">全体准备</button>
        <button class="ghost" :disabled="!canLeaveAll" @click="leaveAll">全部离开</button>
        <button class="ghost" @click="backLobby">返回大厅</button>
      </div>
    </header>
    <div class="grid">
      <PhzLabPanel :ref="(el) => setPanel(el, 2)" class="s2" :seat-index="2" :device-id="devices[2]!" />
      <PhzLabPanel :ref="(el) => setPanel(el, 1)" class="s1" :seat-index="1" :device-id="devices[1]!" />
      <PhzLabPanel :ref="(el) => setPanel(el, 0)" class="s0" :seat-index="0" :device-id="devices[0]!" />
    </div>
  </div>
</template>

<style scoped>
.lab {
  min-height: 100vh;
  padding: 16px 18px 24px;
  background:
    radial-gradient(ellipse at top, rgba(55, 85, 55, 0.5), transparent 55%),
    linear-gradient(180deg, #1a2e1c 0%, #0d1610 100%);
  color: #e8f2ec;
}
.bar {
  display: flex;
  flex-wrap: wrap;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}
.bar h1 { margin: 0; font-size: 1.4rem; }
.tip { margin: 4px 0 0; color: #9db5a8; font-size: 13px; }
.actions { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  grid-template-areas:
    's2 s1'
    's0 s0';
  gap: 12px;
}
.s2 { grid-area: s2; }
.s1 { grid-area: s1; }
.s0 { grid-area: s0; }
@media (max-width: 900px) {
  .grid {
    grid-template-columns: 1fr;
    grid-template-areas:
      's0'
      's1'
      's2';
  }
}
button {
  border: 0;
  background: rgba(255, 255, 255, 0.12);
  color: #e8f2ec;
  padding: 8px 12px;
  border-radius: 8px;
  cursor: pointer;
}
button:disabled { opacity: 0.45; }
button.primary {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-weight: 700;
}
button.ghost { background: transparent; border: 1px solid rgba(255, 255, 255, 0.22); }
</style>