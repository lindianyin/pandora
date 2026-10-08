<script setup lang="ts">
import { ref } from 'vue'
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

function sleep(ms: number) {
  return new Promise((r) => setTimeout(r, ms))
}
</script>

<template>
  <div class="lab">
    <header class="bar">
      <h1>跑胡子三联调试</h1>
      <div class="actions">
        <button :disabled="booting" @click="bootAll">一键登录</button>
        <button @click="matchAll">一键匹配</button>
        <button @click="readyAll">一键准备</button>
        <router-link to="/lobby">回大厅</router-link>
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
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.bar {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
.bar h1 {
  margin: 0;
  font-size: 20px;
}
.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  align-items: center;
}
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  grid-template-areas:
    's2 s1'
    's0 s0';
  gap: 10px;
}
.s2 {
  grid-area: s2;
}
.s1 {
  grid-area: s1;
}
.s0 {
  grid-area: s0;
}
@media (max-width: 900px) {
  .grid {
    grid-template-columns: 1fr;
    grid-template-areas:
      's0'
      's1'
      's2';
  }
}
</style>
