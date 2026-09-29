import fs from 'fs'

const panel = String.raw`<script setup lang="ts">
import { onBeforeUnmount } from 'vue'
import { createLabClient } from '../composables/createLabClient'
import type { HzmjMeld } from '../net/hzmjFront'

const props = defineProps<{ slotIndex: number }>()
const client = createLabClient(props.slotIndex)

const {
  label,
  nickname,
  uid,
  wsOk,
  busy,
  logs,
  errorBanner,
  matching,
  roomId,
  roomPhase,
  seats,
  mySeat,
  hand,
  selectedHandIndex,
  turnSeat,
  countdown,
  cardsLeft,
  lastPlays,
  hzmjBanker,
  hzmjN,
  hzmjWall,
  hzmjSub,
  hzmjLastDiscard,
  hzmjMelds,
  hzmjRivers,
  hzmjSettle,
  showSettle,
  hzmjClaimSent,
  hzmjClaimHint,
  hzmjCaishen,
  isMyTurn,
  iAmReady,
  canReady,
  isHzmjClaim,
  isHzmjDiscardTurn,
  anGangCandidates,
  canClaimChi,
  canClaimPeng,
  canClaimGang,
  canClaimHu,
  login,
  matchHzmj,
  doReady,
  selectTile,
  doDiscard,
  doAction,
  doAnGang,
  leave,
  dispose,
  tileLabel,
  meldKindLabel,
} = client

onBeforeUnmount(() => dispose())

defineExpose({
  login,
  matchHzmj,
  doReady,
  wsOk,
  roomId,
})

function isCaishen(t: number) {
  return hzmjCaishen.value.includes(t)
}

function seatMelds(seatId: number): HzmjMeld[] {
  return hzmjMelds.value[seatId] || []
}
</script>

<template>
  <section class="panel" :class="{ active: isHzmjDiscardTurn || isHzmjClaim, claim: isHzmjClaim }">
    <div class="ph">
      <strong>{{ label }}</strong>
      <span v-if="uid">uid {{ uid }} {{ nickname }}</span>
      <span v-else>offline</span>
      <span v-if="mySeat >= 0">\u5ea7{{ mySeat }}</span>
      <span v-if="mySeat === hzmjBanker" class="tag">\u5e84</span>
      <span class="meta">{{ roomPhase || '-' }} · \u724c\u5899 {{ hzmjWall }} · N={{ hzmjN }}</span>
    </div>
    <div v-if="errorBanner" class="err">{{ errorBanner }}</div>
    <div class="row">
      <button v-if="!wsOk" :disabled="busy" @click="login()">\u767b\u5f55</button>
      <button v-else-if="!roomId" :disabled="matching" @click="matchHzmj()">\u5339\u914d</button>
      <button v-if="canReady" :disabled="iAmReady" @click="doReady()">
        {{ iAmReady ? 'OK' : '\u51c6\u5907' }}
      </button>
      <button v-if="roomId" class="ghost" @click="leave()">\u79bb\u5f00</button>
    </div>

    <div v-if="countdown > 0 && hzmjSub" class="cd">
      {{ isHzmjClaim ? '\u9e23\u724c' : isMyTurn ? '\u4f60\u7684\u56de\u5408' : '\u5ea7' + turnSeat }}
      · {{ hzmjSub }} · {{ countdown }}s
    </div>

    <div v-if="hzmjLastDiscard" class="disc">
      \u51fa\u724c seat{{ hzmjLastDiscard.seat }}
      <b>{{ tileLabel(hzmjLastDiscard.tile) }}</b>
    </div>

    <div class="seats">
      <div
        v-for="s in seats"
        :key="s.seat_id"
        class="mini"
        :class="{ me: s.seat_id === mySeat, turn: s.seat_id === turnSeat }"
      >
        <div>{{ s.nickname }} \u5ea7{{ s.seat_id }} \u5269{{ cardsLeft[s.seat_id] ?? '-' }}</div>
        <div class="melds">
          <span v-for="(m, mi) in seatMelds(s.seat_id)" :key="mi" class="mg">
            {{ meldKindLabel(m.kind) }}
            <i v-for="(t, ti) in m.tiles" :key="ti">{{ tileLabel(t) }}</i>
          </span>
        </div>
        <div class="river">
          <i v-for="(t, ti) in hzmjRivers[s.seat_id] || []" :key="ti">{{ tileLabel(t) }}</i>
        </div>
        <div class="lp">{{ lastPlays[s.seat_id] || '' }}</div>
      </div>
    </div>

    <div class="hand-label">\u624b\u724c</div>
    <div class="hand">
      <button
        v-for="(t, idx) in hand"
        :key="idx + '-' + t"
        class="tile"
        :class="{ on: selectedHandIndex === idx, cai: isCaishen(t) }"
        @click="selectTile(idx)"
      >
        {{ tileLabel(t) }}
      </button>
    </div>

    <div class="row">
      <template v-if="isHzmjDiscardTurn">
        <button :disabled="selectedHandIndex == null" @click="doDiscard()">\u51fa\u724c</button>
        <button v-for="g in anGangCandidates" :key="'g' + g" class="warn" @click="doAnGang(g)">
          \u6697\u6760 {{ tileLabel(g) }}
        </button>
      </template>
      <template v-if="isHzmjClaim">
        <span v-if="hzmjClaimHint" class="hint">{{ hzmjClaimHint }}</span>
        <button :disabled="hzmjClaimSent" @click="doAction(0)">\u8fc7</button>
        <button v-if="canClaimChi" :disabled="hzmjClaimSent" @click="doAction(1)">\u5403</button>
        <button v-if="canClaimPeng" :disabled="hzmjClaimSent" @click="doAction(2)">\u78b0</button>
        <button v-if="canClaimGang" :disabled="hzmjClaimSent" @click="doAction(3)">\u6760</button>
        <button v-if="canClaimHu" class="warn" :disabled="hzmjClaimSent" @click="doAction(4)">\u80e1</button>
      </template>
    </div>

    <div v-if="showSettle && hzmjSettle" class="settle">
      <strong>\u7ed3\u7b97</strong>
      M={{ hzmjSettle.M }} N={{ hzmjSettle.N }}
      <span v-for="e in hzmjSettle.entries" :key="e.uid">
        \u5ea7{{ e.seat_id }} {{ e.delta_gold >= 0 ? '+' : '' }}{{ e.delta_gold }}
        <em v-if="e.uid === uid">(\u6211)</em>
      </span>
    </div>

    <pre class="log">{{ logs.slice(0, 8).join('\n') }}</pre>
  </section>
</template>

<style scoped>
.panel {
  background: #fff;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  padding: 10px;
  font-size: 12px;
}
.panel.active { box-shadow: 0 0 0 2px #f59e0b; }
.panel.claim { box-shadow: 0 0 0 2px #0f766e; }
.ph { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 6px; }
.meta { color: #64748b; }
.tag { background: #b45309; color: #fff; padding: 1px 6px; border-radius: 4px; font-size: 11px; }
.err { background: #fef2f2; color: #b91c1c; padding: 4px 8px; border-radius: 4px; margin-bottom: 6px; }
.row { display: flex; flex-wrap: wrap; gap: 6px; margin: 6px 0; }
.cd { background: #fef3c7; color: #b45309; padding: 4px 8px; border-radius: 999px; display: inline-block; margin-bottom: 6px; }
.disc { font-weight: 600; margin-bottom: 6px; }
.seats { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; margin-bottom: 6px; }
.mini { border: 1px solid #e2e8f0; border-radius: 6px; padding: 4px 6px; background: #f8fafc; }
.mini.me { border-color: #0f766e; background: #f0fdfa; }
.mini.turn { box-shadow: 0 0 0 1px #f59e0b; }
.melds, .river { display: flex; flex-wrap: wrap; gap: 2px; margin-top: 2px; }
.mg { background: #e2e8f0; border-radius: 4px; padding: 1px 3px; margin-right: 4px; }
.mg i, .river i { font-style: normal; border: 1px solid #cbd5e1; background: #fff; padding: 0 3px; border-radius: 3px; margin-left: 1px; }
.lp { min-height: 1em; font-weight: 600; }
.hand-label { color: #64748b; margin-top: 4px; }
.hand { display: flex; flex-wrap: wrap; gap: 4px; min-height: 36px; }
.tile {
  border: 1px solid #cbd5e1;
  background: #f8fafc;
  border-radius: 4px;
  padding: 4px 6px;
  cursor: pointer;
  color: #0f172a;
}
.tile.on { background: #0f766e; color: #fff; border-color: #0f766e; }
.tile.cai { border-color: #f59e0b; }
button {
  border: 0;
  background: #0f766e;
  color: #fff;
  padding: 6px 10px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 12px;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.ghost { background: #e2e8f0; color: #334155; }
button.warn { background: #c2410c; }
.hint { color: #b45309; align-self: center; }
.settle { background: #ecfdf5; padding: 6px; border-radius: 6px; margin-top: 6px; }
.log {
  margin: 8px 0 0;
  max-height: 90px;
  overflow: auto;
  background: #0f172a;
  color: #cbd5e1;
  padding: 6px;
  border-radius: 6px;
  font-size: 10px;
}
</style>
`

const lab = String.raw`<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import HzmjLabPanel from './HzmjLabPanel.vue'

const router = useRouter()
const booting = ref(false)
const panels = ref<InstanceType<typeof HzmjLabPanel>[]>([])

function setPanel(el: unknown, i: number) {
  if (el) panels.value[i] = el as InstanceType<typeof HzmjLabPanel>
}

async function bootAll() {
  booting.value = true
  try {
    for (const p of panels.value) {
      if (p && !p.wsOk) await p.login()
      await sleep(200)
    }
  } finally {
    booting.value = false
  }
}

async function matchAll() {
  for (const p of panels.value) {
    if (p && p.wsOk && !p.roomId) p.matchHzmj()
    await sleep(80)
  }
}

async function readyAll() {
  for (const p of panels.value) {
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
        <h1>\u676d\u5dde\u9ebb\u5c06\u00b7\u56db\u8054\u8c03\u8bd5</h1>
        <p class="tip">\u540c\u9875\u56db\u5ba2\u6237\uff1b\u8c03\u8bd5\u53d1\u724c\u65f6 seat0 \u542c\u300c\u4e1c\u300d\u53ef\u70b9\u70ae</p>
      </div>
      <div class="ops">
        <button :disabled="booting" @click="bootAll">\u4e00\u952e\u767b\u5f55\u56db\u4eba</button>
        <button @click="matchAll">\u5168\u5458\u5339\u914d</button>
        <button @click="readyAll">\u5168\u5458\u51c6\u5907</button>
        <button class="ghost" @click="router.push('/lobby')">\u8fd4\u56de\u5927\u5385</button>
      </div>
    </header>

    <div class="grid">
      <HzmjLabPanel v-for="i in 4" :key="i" :slot-index="i - 1" :ref="(el) => setPanel(el, i - 1)" />
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
  gap: 12px;
}
@media (max-width: 1100px) {
  .grid { grid-template-columns: 1fr; }
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
`

function decodeEscapes(s) {
  return s.replace(/\\u([0-9a-fA-F]{4})/g, (_, h) => String.fromCharCode(parseInt(h, 16)))
}

fs.writeFileSync('src/views/HzmjLabPanel.vue', decodeEscapes(panel), 'utf8')
fs.writeFileSync('src/views/HzmjLabView.vue', decodeEscapes(lab), 'utf8')
console.log('ok', fs.readFileSync('src/views/HzmjLabView.vue','utf8').includes('\u676d\u5dde'))
