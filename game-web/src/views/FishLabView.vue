<script setup lang="ts">
import { computed, onMounted, shallowRef } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { createFishLabClient } from '../composables/createFishLabClient'

const router = useRouter()
const route = useRoute()

function queryOne(v: unknown): string {
  const s = Array.isArray(v) ? v[0] : v
  return typeof s === 'string' ? s.trim().slice(0, 128) : ''
}

/** d0–d1, or hw=a,b. Missing slots filled and written back so refresh keeps the same guests. */
function labDeviceIds(): string[] {
  const q = route.query
  const packed = queryOne(q.hw || q.devices)
    .split(/[,|]/)
    .map((s) => s.trim())
    .filter(Boolean)
  const ids = [0, 1].map((i) => queryOne(q[`d${i}`] ?? q[`hw${i}`]) || packed[i] || '')
  const missing = ids.some((s) => !s)
  for (let i = 0; i < 2; i++) {
    if (!ids[i]) ids[i] = `fishlab${i}-${Math.random().toString(16).slice(2, 10)}`
  }
  if (missing) {
    const query: Record<string, string> = {}
    for (const [k, v] of Object.entries(q)) {
      const one = queryOne(v)
      if (one && !/^d[0-1]$/.test(k) && k !== 'hw' && k !== 'devices' && !/^hw[0-1]$/.test(k)) {
        query[k] = one
      }
    }
    ids.forEach((id, i) => {
      query[`d${i}`] = id
    })
    router.replace({ path: '/fish-lab', query })
  }
  return ids
}

const devices = labDeviceIds()
const clients = shallowRef([
  createFishLabClient(0, devices[0]!),
  createFishLabClient(1, devices[1]!),
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
    <header>
      <div>
        <h1>捕鱼 Lab</h1>
        <p>
          硬件码已写入地址栏
          <code>d0</code>/<code>d1</code>
          ；刷新保持同一游客。双端同 template。
        </p>
        <p class="hw">
          <span v-for="(id, i) in devices" :key="id">d{{ i }}={{ id }} </span>
        </p>
      </div>
      <div class="toolbar">
        <button class="primary" :disabled="!canMatchAll" @click="matchAll">一键匹配</button>
        <button class="primary" :disabled="!canReadyAll" @click="readyAll">一键准备</button>
        <button class="warn" :disabled="!canLeaveAll" @click="leaveAll">一键离开</button>
        <button class="ghost" @click="topupAll">补给金币</button>
        <button class="ghost" @click="backLobby">返回大厅</button>
      </div>
    </header>
    <div class="grid">
      <section v-for="(c, i) in clients" :key="c.label" class="panel">
        <h2>
          {{ c.label }} · uid={{ c.uid.value }} · 金={{ c.gold.value }}
          · 本发 {{ c.fireCost.value }}
        </h2>
        <div class="meta">
          <span>hw {{ devices[i] }}</span>
          <span>房间 {{ c.roomId.value || '-' }}</span>
          <span>局号 {{ c.roundId.value || '-' }}</span>
          <span>相位 {{ c.roomPhase.value || '-' }}</span>
          <span v-if="c.mySeat.value >= 0">座 {{ c.mySeat.value }}</span>
          <span>底分 {{ c.baseScore.value }}</span>
        </div>
        <p v-if="c.errorBanner.value" class="err">{{ c.errorBanner.value }}</p>
        <p v-else-if="c.matching.value" class="hint">匹配中… {{ c.matchMsg.value }}</p>
        <p v-else-if="c.iAmReady.value && !c.roundId.value" class="hint">已准备</p>
        <div class="row">
          <button :disabled="!c.canMatch.value" @click="c.oneClickMatch()">匹配</button>
          <button :disabled="!c.canReady.value" @click="c.oneClickReady()">准备</button>
          <button :disabled="!c.roundId.value" @click="c.fire()">开火</button>
          <button
            :disabled="!(c.roomId.value || c.roundId.value || c.matching.value)"
            @click="c.oneClickLeave()"
          >
            离开
          </button>
          <button @click="c.topupGold()">补给金币</button>
        </div>
        <div class="row">
          <label>炮倍</label>
          <select :value="c.mult.value" @change="c.setMult(Number(($event.target as HTMLSelectElement).value))">
            <option v-for="m in c.cannonMults.value" :key="m" :value="m">{{ m }}</option>
          </select>
          <label>aimX</label>
          <input v-model.number="c.aimX.value" type="number" />
          <label>aimY</label>
          <input v-model.number="c.aimY.value" type="number" />
        </div>
        <div class="pond">
          <div
            v-for="f in c.fishList.value"
            :key="f.fish_id"
            class="fish"
            :style="{ left: (f.x / 1920) * 100 + '%', bottom: (f.y / 1080) * 100 + '%' }"
          >
            {{ f.type_id }}
          </div>
          <div
            v-for="b in c.bullets.value"
            :key="b.id"
            class="bullet"
            :style="{ left: (b.x / 1920) * 100 + '%', bottom: (b.y / 1080) * 100 + '%' }"
          />
        </div>
        <pre class="log">{{ c.logs.value.join('\n') }}</pre>
      </section>
    </div>
  </div>
</template>

<style scoped>
.lab { padding: 16px; background: #0b1f2a; color: #e8f4f8; min-height: 100vh; }
header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 12px;
  flex-wrap: wrap;
}
header h1 { margin: 0; font-family: "Segoe UI", sans-serif; }
header p { margin: 4px 0 0; opacity: 0.8; font-size: 13px; }
.hw { font-family: ui-monospace, Consolas, monospace; font-size: 12px; word-break: break-all; }
.toolbar { display: flex; gap: 8px; flex-wrap: wrap; }
.toolbar .primary {
  background: #2a9d8f;
  color: #fff;
  border: 0;
  padding: 8px 14px;
  border-radius: 6px;
  font-weight: 600;
}
.toolbar .primary:disabled,
.toolbar .warn:disabled { opacity: 0.4; }
.toolbar .warn {
  background: #c45c26;
  color: #fff;
  border: 0;
  padding: 8px 14px;
  border-radius: 6px;
  font-weight: 600;
}
.toolbar .ghost {
  background: transparent;
  color: #cde;
  border: 1px solid #3a5a6a;
  padding: 8px 14px;
  border-radius: 6px;
}
.grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 12px; }
.panel { background: #123040; border-radius: 8px; padding: 12px; }
.meta {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  font-size: 13px;
  opacity: 0.9;
  margin: 4px 0 8px;
}
.row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin: 8px 0; }
.pond {
  position: relative;
  height: 220px;
  background: linear-gradient(180deg, #1a5a7a, #0d3040);
  border-radius: 6px;
  overflow: hidden;
}
.fish {
  position: absolute;
  width: 28px; height: 18px;
  background: #f0c14b;
  border-radius: 40%;
  font-size: 10px;
  text-align: center;
  transform: translate(-50%, 50%);
}
.bullet {
  position: absolute;
  width: 6px; height: 6px;
  background: #fff;
  border-radius: 50%;
  transform: translate(-50%, 50%);
}
.log { max-height: 120px; overflow: auto; font-size: 11px; background: #0a1820; padding: 8px; }
.err { color: #ff8a80; }
.hint { color: #9ad7c8; font-size: 13px; }
button { cursor: pointer; }
code { background: #0a1820; padding: 1px 4px; border-radius: 3px; }
@media (max-width: 900px) { .grid { grid-template-columns: 1fr; } }
</style>
