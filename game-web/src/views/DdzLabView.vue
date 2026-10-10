<script setup lang="ts">
import { computed, nextTick, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import DdzLabPanel from './DdzLabPanel.vue'

const router = useRouter()
const route = useRoute()
const booting = ref(false)
const panels = ref<InstanceType<typeof DdzLabPanel>[]>([])

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
    if (!ids[i]) ids[i] = `ddzlab${i}-${Math.random().toString(16).slice(2, 10)}`
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
    router.replace({ path: '/ddz-lab', query })
  }
  return ids
}

const devices = labDeviceIds()

function setPanel(el: unknown, i: number) {
  if (el) panels.value[i] = el as InstanceType<typeof DdzLabPanel>
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
    if (p && p.wsOk && !p.roomId) p.matchDdz()
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
        <h1>斗地主 · 三联调试</h1>
        <p class="tip">进入后自动登录三人；硬件码写入 d0–d2，刷新同一游客。</p>
      </div>
      <div class="ops">
        <button class="primary" :disabled="booting" @click="matchAll">一键匹配</button>
        <button class="primary" @click="readyAll">全体准备</button>
        <button class="ghost" :disabled="!canLeaveAll" @click="leaveAll">全部离开</button>
        <button class="ghost" @click="backLobby">返回大厅</button>
      </div>
    </header>
    <div class="grid">
      <DdzLabPanel
        v-for="i in 3"
        :key="i"
        :slot-index="i - 1"
        :device-id="devices[i - 1] || ''"
        :ref="(el) => setPanel(el, i - 1)"
      />
    </div>
  </div>
</template>

<style scoped>
.lab {
  min-height: 100vh;
  margin: 0;
  padding: 16px 18px 24px;
  background:
    radial-gradient(ellipse at top, rgba(50, 100, 70, 0.5), transparent 55%),
    linear-gradient(180deg, #123226 0%, #0b1a14 100%);
  color: #e8f2ec;
}
.bar {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.bar h1 { margin: 0; font-size: 1.4rem; }
.tip { margin: 4px 0 0; color: #9db5a8; font-size: 13px; }
.ops { display: flex; gap: 8px; flex-wrap: wrap; }
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  grid-template-areas:
    "s2 s1"
    "s0 s0";
  gap: 12px;
}
:deep(.pos-s0) { grid-area: s0; }
:deep(.pos-s1) { grid-area: s1; }
:deep(.pos-s2) { grid-area: s2; }
@media (max-width: 1100px) {
  .grid {
    grid-template-columns: 1fr;
    grid-template-areas:
      "s2"
      "s1"
      "s0";
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