<script setup lang="ts">
import { computed, onMounted, shallowRef } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { createSssLabClient } from '../composables/createSssLabClient'
import SssSeatPanel from '../components/SssSeatPanel.vue'

const router = useRouter()
const route = useRoute()

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
  const ids = [0, 1, 2, 3].map((i) => queryOne(q[`d${i}`] ?? q[`hw${i}`]) || packed[i] || '')
  const missing = ids.some((s) => !s)
  for (let i = 0; i < 4; i++) {
    if (!ids[i]) ids[i] = `ssslab${i}-${Math.random().toString(16).slice(2, 10)}`
  }
  if (missing) {
    const query: Record<string, string> = {}
    for (const [k, v] of Object.entries(q)) {
      const one = queryOne(v)
      if (one && !/^d[0-3]$/.test(k) && k !== 'hw' && k !== 'devices' && !/^hw[0-3]$/.test(k)) {
        query[k] = one
      }
    }
    ids.forEach((id, i) => {
      query[`d${i}`] = id
    })
    router.replace({ path: '/sss-lab', query })
  }
  return ids
}

const devices = labDeviceIds()
const clients = shallowRef([
  createSssLabClient(0, devices[0]!),
  createSssLabClient(1, devices[1]!),
  createSssLabClient(2, devices[2]!),
  createSssLabClient(3, devices[3]!),
])

const canMatchAll = computed(() => clients.value.every((c) => c.canMatch.value))
const canReadyAll = computed(() => clients.value.some((c) => c.canReady.value))
const canLeaveAll = computed(() =>
  clients.value.some((c) => c.roomId.value || c.roundId.value || c.matching.value),
)

function matchAll() {
  for (const c of clients.value) c.oneClickMatch()
}
function readyAll() {
  for (const c of clients.value) c.oneClickReady()
}
function leaveAll() {
  for (const c of clients.value) c.oneClickLeave()
}
function autoAll() {
  for (const c of clients.value) c.oneClickConfirm()
}
function smartAll() {
  for (const c of clients.value) c.smartFill()
}
function topupAll() {
  for (const c of clients.value) c.topupGold()
}
function backLobby() {
  leaveAll()
  router.push('/lobby')
}

onMounted(() => {
  for (const c of clients.value) c.login()
})
</script>

<template>
  <div class="lab">
    <header class="bar">
      <div>
        <h1>十三水 Lab</h1>
        <p>四端同桌 · 头3中5尾5 · 超时服务端自动摆牌</p>
        <p class="hw">
          <span v-for="(id, i) in devices" :key="id">d{{ i }}={{ id }} </span>
        </p>
      </div>
      <div class="actions">
        <button class="primary" :disabled="!canMatchAll" @click="matchAll">一键匹配</button>
        <button class="primary" :disabled="!canReadyAll" @click="readyAll">全体准备</button>
        <button @click="smartAll">全体智能摆牌</button>
        <button @click="autoAll">全体智能摆牌一键确认</button>
        <button @click="topupAll">加币</button>
        <button class="ghost" :disabled="!canLeaveAll" @click="leaveAll">全部离开</button>
        <button class="ghost" @click="backLobby">大厅</button>
      </div>
    </header>
    <div class="grid">
      <SssSeatPanel
        v-for="(c, i) in clients"
        :key="c.label"
        :client="c"
        :device-id="devices[i]!"
        compact
      />
    </div>
  </div>
</template>

<style scoped>
.lab {
  min-height: 100vh;
  margin: -12px -16px;
  padding: 8px 10px 12px;
  background:
    radial-gradient(ellipse at top, rgba(40, 90, 70, 0.55), transparent 55%),
    linear-gradient(180deg, #10241c 0%, #0b1712 100%);
  color: #e8f2ec;
}
.bar {
  display: flex;
  justify-content: space-between;
  gap: 8px;
  flex-wrap: wrap;
  margin-bottom: 8px;
  align-items: center;
}
.bar h1 {
  margin: 0;
  font-size: 1.15rem;
  letter-spacing: 0.03em;
}
.bar p { margin: 2px 0 0; opacity: 0.75; font-size: 11px; }
.hw { display: none; }
.actions { display: flex; gap: 5px; flex-wrap: wrap; align-items: flex-start; }
.actions button {
  border: 0;
  border-radius: 6px;
  padding: 5px 8px;
  font-size: 12px;
  background: rgba(255, 255, 255, 0.1);
  color: #e8f2ec;
  cursor: pointer;
}
.actions button:disabled { opacity: 0.4; cursor: not-allowed; }
.actions .primary {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-weight: 700;
}
.actions .ghost { background: transparent; border: 1px solid rgba(255,255,255,0.22); }
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 6px;
}
@media (max-width: 1100px) {
  .grid { grid-template-columns: 1fr; }
}
</style>
