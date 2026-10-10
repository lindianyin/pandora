<script setup lang="ts">
import { computed, onMounted, shallowRef } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import FishCannon from '../components/FishCannon.vue'
import { createFishLabClient } from '../composables/createFishLabClient'
import { fishVisual } from '../net/frame'

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
    <header class="bar">
      <div>
        <h1>捕鱼 Lab</h1>
        <p>进入自动登录双端 · 点击渔场开火 · 硬件码 d0/d1 刷新保持游客</p>
        <p class="hw">
          <span v-for="(id, i) in devices" :key="id">d{{ i }}={{ id }} </span>
        </p>
      </div>
      <div class="toolbar">
        <button class="primary" :disabled="!canMatchAll" @click="matchAll">一键匹配</button>
        <button class="primary" :disabled="!canReadyAll" @click="readyAll">全体准备</button>
        <button class="ghost" :disabled="!canLeaveAll" @click="leaveAll">全部离开</button>
        <button class="ghost" @click="topupAll">补给金币</button>
        <button class="ghost" @click="backLobby">返回大厅</button>
      </div>
    </header>

    <div class="grid">
      <section v-for="(c, i) in clients" :key="c.label" class="panel">
        <div class="ph">
          <div>
            <h2>{{ c.label }} · {{ c.nickname.value || '…' }}</h2>
            <p class="meta">
              uid {{ c.uid.value || '-' }} · 房 {{ c.roomId.value || '-' }} · 局
              {{ c.roundId.value || '-' }} · {{ c.roomPhase.value || 'idle' }}
              <span v-if="c.mySeat.value >= 0"> · 座 {{ c.mySeat.value }}</span>
            </p>
            <p class="hw">{{ devices[i] }}</p>
          </div>
          <div class="gold-pill">
            <span>金币</span>
            <strong>{{ c.gold.value }}</strong>
            <em>本发 {{ c.fireCost.value }}</em>
          </div>
        </div>

        <p v-if="c.errorBanner.value" class="err">{{ c.errorBanner.value }}</p>
        <p v-else-if="c.matching.value" class="hint">匹配中… {{ c.matchMsg.value }}</p>
        <p v-else-if="c.iAmReady.value && !c.roundId.value" class="hint">已准备</p>
        <p v-else-if="c.roundId.value" class="hint">点击渔场开火</p>

        <div class="row">
          <button class="primary" :disabled="!c.canMatch.value" @click="c.oneClickMatch()">匹配</button>
          <button class="primary" :disabled="!c.canReady.value" @click="c.oneClickReady()">准备</button>
          <button :disabled="!c.roundId.value" @click="c.fire()">开火</button>
          <button
            class="ghost"
            :disabled="!(c.roomId.value || c.roundId.value || c.matching.value)"
            @click="c.oneClickLeave()"
          >
            离开
          </button>
          <button class="ghost" @click="c.topupGold()">补给</button>
          <label class="mult">
            炮倍
            <select
              :value="c.mult.value"
              @change="c.setMult(Number(($event.target as HTMLSelectElement).value))"
            >
              <option v-for="m in c.cannonMults.value" :key="m" :value="m">{{ m }}×</option>
            </select>
          </label>
        </div>

        <div class="pond" @mousemove="c.aimAtPond($event)" @click="c.fireAtPond($event)">
          <div class="caustic" />
          <div
            v-for="f in c.fishList.value"
            :key="f.fish_id"
            class="fish"
            :class="[fishVisual(f.type_id, f.radius).cls, { flip: f.vx < 0 }]"
            :style="{
              left: (f.x / 1920) * 100 + '%',
              bottom: (f.y / 1080) * 100 + '%',
              width: fishVisual(f.type_id, f.radius).size + 'px',
              height: fishVisual(f.type_id, f.radius).size * 0.62 + 'px',
            }"
            :title="fishVisual(f.type_id, f.radius).label"
          >
            <span class="eye" />
            <span class="fin" />
          </div>
          <FishCannon
            v-for="cn in c.cannons.value"
            :key="'c' + cn.seat"
            :left-pct="cn.leftPct"
            :bottom-pct="cn.bottomPct"
            :angle="cn.angle"
            :mult="cn.mult"
            :nickname="cn.nickname"
            :me="cn.me"
            :firing="cn.firing"
            :empty="cn.empty"
            :top="cn.top"
          />
          <div
            v-for="b in c.bullets.value"
            :key="b.id"
            class="bullet"
            :style="{ left: (b.x / 1920) * 100 + '%', bottom: (b.y / 1080) * 100 + '%' }"
          />
          <div class="pond-hint" v-if="!c.roundId.value">渔场待命</div>
        </div>

        <pre class="log">{{ c.logs.value.join('\n') }}</pre>
      </section>
    </div>
  </div>
</template>

<style scoped>
.lab {
  min-height: 100vh;
  padding: 16px 18px 24px;
  background:
    radial-gradient(ellipse at top, rgba(30, 120, 160, 0.45), transparent 55%),
    linear-gradient(180deg, #062636 0%, #03151f 100%);
  color: #e8f4f8;
}
.bar {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
  margin-bottom: 12px;
}
.bar h1 { margin: 0; font-size: 1.45rem; letter-spacing: 0.04em; }
.bar p { margin: 4px 0 0; opacity: 0.8; font-size: 13px; }
.hw { font-family: ui-monospace, Consolas, monospace; font-size: 12px; word-break: break-all; color: #7fb0c4; }
.toolbar { display: flex; gap: 8px; flex-wrap: wrap; }
.toolbar button, .row button {
  border: 0;
  border-radius: 8px;
  padding: 8px 12px;
  background: rgba(255, 255, 255, 0.1);
  color: #e8f4f8;
  cursor: pointer;
}
.toolbar button:disabled, .row button:disabled { opacity: 0.4; cursor: not-allowed; }
.primary {
  background: linear-gradient(180deg, #3ec6b8, #1f8f84) !important;
  color: #04201d !important;
  font-weight: 700;
}
.ghost {
  background: transparent !important;
  border: 1px solid rgba(255, 255, 255, 0.2) !important;
}
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
.panel {
  background: linear-gradient(160deg, #0d3a4e 0%, #0a2a3a 55%, #071f2b 100%);
  border: 1px solid rgba(120, 200, 230, 0.18);
  border-radius: 16px;
  padding: 12px;
  box-shadow: 0 10px 28px rgba(0, 0, 0, 0.28);
}
.ph {
  display: flex;
  justify-content: space-between;
  gap: 10px;
  align-items: flex-start;
  margin-bottom: 8px;
}
.ph h2 { margin: 0; font-size: 1.05rem; }
.meta { margin: 4px 0 0; font-size: 12px; color: #9fc4d4; }
.gold-pill {
  display: inline-flex;
  align-items: baseline;
  gap: 6px;
  background: rgba(0, 0, 0, 0.28);
  border: 1px solid rgba(240, 209, 106, 0.4);
  border-radius: 999px;
  padding: 6px 12px;
  color: #f0d16a;
  white-space: nowrap;
}
.gold-pill strong { font-size: 1.1rem; }
.gold-pill em { font-style: normal; color: #9ad7c8; font-size: 12px; }
.err { color: #ff8a80; margin: 0 0 6px; font-size: 13px; }
.hint { color: #9ad7c8; margin: 0 0 6px; font-size: 13px; }
.row {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  align-items: center;
  margin-bottom: 8px;
}
.mult {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: #9fc4d4;
}
.mult select {
  background: rgba(0, 0, 0, 0.35);
  color: #e8f4f8;
  border: 1px solid rgba(255, 255, 255, 0.18);
  border-radius: 6px;
  padding: 5px 8px;
}
.pond {
  position: relative;
  height: 260px;
  border-radius: 14px;
  overflow: hidden;
  cursor: crosshair;
  background:
    radial-gradient(ellipse at 30% 20%, rgba(80, 200, 255, 0.18), transparent 45%),
    linear-gradient(180deg, #1a7ca3 0%, #0c4a66 45%, #072839 100%);
  border: 1px solid rgba(120, 200, 230, 0.2);
  margin-bottom: 8px;
}
.caustic {
  position: absolute;
  inset: 0;
  background: radial-gradient(circle at 40% 30%, rgba(255, 255, 255, 0.08), transparent 30%);
  pointer-events: none;
}
.fish {
  position: absolute;
  transform: translate(-50%, 50%);
  border-radius: 50% 45% 50% 45%;
  box-shadow: 0 3px 8px rgba(0, 0, 0, 0.28);
  z-index: 2;
}
.bullet { z-index: 4; }
.fish.flip { transform: translate(-50%, 50%) scaleX(-1); }
.fish.t1 { background: linear-gradient(180deg, #7ee8fa, #22a6c7); }
.fish.t2 { background: linear-gradient(180deg, #ffe08a, #e0a12b); }
.fish.t3 { background: linear-gradient(180deg, #ff9a6b, #d9480f); }
.fish.t0 { background: linear-gradient(180deg, #c4b5fd, #7c3aed); }
.eye {
  position: absolute;
  right: 22%;
  top: 32%;
  width: 18%;
  height: 28%;
  border-radius: 50%;
  background: #111;
  box-shadow: inset -1px -1px 0 #fff;
}
.fin {
  position: absolute;
  left: -10%;
  width: 22%;
  height: 55%;
  background: inherit;
  clip-path: polygon(100% 0, 0 50%, 100% 100%);
  filter: brightness(0.9);
}
.bullet {
  position: absolute;
  width: 7px;
  height: 7px;
  background: #fff8c8;
  border-radius: 50%;
  box-shadow: 0 0 8px #ffe08a;
  transform: translate(-50%, 50%);
}
.pond-hint {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: rgba(232, 244, 248, 0.4);
  letter-spacing: 0.1em;
  pointer-events: none;
}
.log {
  margin: 0;
  max-height: 110px;
  overflow: auto;
  font-size: 11px;
  background: rgba(0, 0, 0, 0.28);
  color: #9fc4d4;
  padding: 8px;
  border-radius: 8px;
  white-space: pre-wrap;
}
@media (max-width: 900px) {
  .grid { grid-template-columns: 1fr; }
}
</style>