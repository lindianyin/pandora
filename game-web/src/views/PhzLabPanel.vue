<script setup lang="ts">
import { createPhzLabClient } from '../composables/createPhzLabClient'
import { meldKindLabel, isDarkMeld } from '../net/phzFront'
import { phzIsRed } from '../net/frame'

const props = defineProps<{ seatIndex: number; deviceId: string }>()
const c = createPhzLabClient(props.seatIndex, props.deviceId)

defineExpose({
  login: () => c.login(),
  matchPhz: () => c.matchPhz(),
  doReady: () => c.doReady(),
  leave: () => c.leave(),
  get wsOk() {
    return c.wsOk.value
  },
  get roomId() {
    return c.roomId.value
  },
  get matching() {
    return c.matching.value
  },
})
</script>

<template>
  <section
    class="panel"
    :class="{
      active: c.isDiscardTurn.value || c.isClaimTurn.value,
      claim: c.isClaimTurn.value,
    }"
  >
    <header>
      <strong>{{ c.label }}</strong>
      <span class="muted">{{ c.nickname.value || props.deviceId }}</span>
      <span v-if="c.uid.value">uid={{ c.uid.value }}</span>
      <span v-if="c.mySeat.value >= 0">座{{ c.mySeat.value }}</span>
      <span>gold={{ c.gold.value }}</span>
    </header>
    <div class="row">
      <button v-if="!c.wsOk.value" :disabled="c.busy.value" @click="c.login()">登录</button>
      <button v-else-if="!c.roomId.value" :disabled="c.matching.value" @click="c.matchPhz()">匹配跑胡子</button>
      <button v-else-if="c.canReady.value && !c.iAmReady.value" class="primary" @click="c.doReady()">准备</button>
      <span v-else-if="c.matching.value" class="muted">匹配中…</span>
      <span v-else-if="c.iAmReady.value" class="muted">已准备</span>
      <button v-if="c.roomId.value || c.matching.value" class="ghost" @click="c.leave()">离开</button>
    </div>
    <p v-if="c.errorBanner.value" class="err">{{ c.errorBanner.value }}</p>
    <div v-if="c.countdown.value > 0 && c.phzSub.value" class="cd">
      {{ c.turnBanner.value }} · {{ c.phzSub.value }} · {{ c.countdown.value }}s
    </div>
    <div v-if="c.phzRoundId.value" class="meta">
      局号 {{ c.phzRoundId.value }} · 庄 seat{{ c.phzBanker.value }} · 墩 {{ c.phzWall.value }}
    </div>
    <div v-if="c.phzReveal.value" class="reveal">
      示众 seat{{ c.phzReveal.value.seat }}：
      <span class="ptile sm" :class="{ red: phzIsRed(c.phzReveal.value.tile) }">{{
        c.phzTileLabel(c.phzReveal.value.tile)
      }}</span>
    </div>
    <div class="melds">
      <div
        v-for="s in 3"
        :key="'m' + s"
        class="meld-seat"
        :class="{ me: s - 1 === c.mySeat.value, turn: s - 1 === c.turnSeat.value }"
      >
        <span class="muted">seat{{ s - 1 }}门前</span>
        <span
          v-for="(m, mi) in c.phzMelds.value[s - 1] || []"
          :key="mi"
          class="meld-group"
          :class="{ dark: isDarkMeld(m.kind) }"
        >
          {{ meldKindLabel(m.kind) }}
          <span
            v-for="(t, ti) in m.tiles"
            :key="ti"
            class="ptile xs"
            :class="{ red: phzIsRed(t) }"
          >{{ c.phzTileLabel(t) }}</span>
        </span>
      </div>
    </div>
    <div class="hand">
      <button
        v-for="(t, i) in c.hand.value"
        :key="i + '-' + t"
        type="button"
        class="ptile"
        :class="{ on: c.selectedHandIndex.value === i, red: phzIsRed(t) }"
        @click="c.selectTile(i)"
      >
        {{ c.phzTileLabel(t) }}
      </button>
    </div>
    <div class="row ops" v-if="c.isDiscardTurn.value || c.isClaimTurn.value">
      <button
        v-if="c.isDiscardTurn.value"
        class="primary"
        :disabled="c.selectedHandIndex.value == null"
        @click="c.discardSelected()"
      >
        出牌
      </button>
      <template v-if="c.isClaimTurn.value">
        <button @click="c.claim(0)">过</button>
        <button v-if="c.canClaimChi.value" @click="c.claim(1)">吃</button>
        <button v-if="c.canClaimPeng.value" @click="c.claim(2)">碰</button>
        <button v-if="c.phzCanHu.value" class="warn" @click="c.claim(4)">胡</button>
      </template>
    </div>
    <div v-if="c.showSettle.value" class="settle">
      <template v-if="c.settle.value">
        <p>
          结算 · 息{{ c.settle.value.hu_xi }} 囤{{ c.settle.value.tun }} 番{{ c.settle.value.fan }} ·
          {{ c.settleMingTang.value }}
        </p>
        <p v-for="e in c.settle.value.entries" :key="e.seat_id">seat{{ e.seat_id }} {{ e.delta_gold }}</p>
      </template>
      <p v-else>流局 · 庄 seat{{ c.liujuBanker.value }}</p>
      <button @click="c.dismissSettle()">关闭</button>
    </div>
    <ul class="logs">
      <li v-for="(line, i) in c.logs.value" :key="i">{{ line }}</li>
    </ul>
  </section>
</template>

<style scoped>
.panel {
  background: linear-gradient(160deg, #2a4630 0%, #1a2e1c 50%, #0d1610 100%);
  color: #e8f2ec;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 14px;
  padding: 12px;
  min-height: 320px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  box-shadow: 0 10px 24px rgba(0, 0, 0, 0.28);
}
.panel.active { box-shadow: 0 0 0 2px #e8c547, 0 10px 24px rgba(0, 0, 0, 0.28); }
.panel.claim { box-shadow: 0 0 0 2px #34d399, 0 10px 24px rgba(0, 0, 0, 0.28); }
.cd {
  background: rgba(232, 197, 71, 0.15);
  color: #e8c547;
  border: 1px solid rgba(232, 197, 71, 0.35);
  padding: 4px 10px;
  border-radius: 999px;
  display: inline-block;
  font-size: 12px;
}
header {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  align-items: baseline;
}
.muted { color: #9db5a8; font-size: 12px; }
.row { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
.err { color: #ffb4b4; margin: 0; font-size: 13px; }
.meta { font-size: 12px; color: #cfe7da; }
.reveal {
  padding: 6px 8px;
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  display: flex;
  align-items: center;
  gap: 6px;
}
.hand { display: flex; flex-wrap: wrap; gap: 5px; min-height: 52px; }
.ptile {
  min-width: 36px;
  height: 50px;
  padding: 0 6px;
  border: 0;
  border-radius: 6px;
  background: linear-gradient(180deg, #fffef8 0%, #efe9da 100%);
  color: #1a1a1a;
  cursor: pointer;
  font-weight: 800;
  box-shadow: 0 2px 0 #c4bba6, 0 4px 10px rgba(0, 0, 0, 0.25);
  display: inline-flex;
  align-items: center;
  justify-content: center;
}
.ptile.sm, .ptile.xs {
  min-width: 24px;
  height: 30px;
  font-size: 12px;
  cursor: default;
}
.ptile.on { outline: 2px solid #e8c547; outline-offset: 1px; transform: translateY(-6px); }
.ptile.red { color: #c62828; }
.meld-seat {
  font-size: 12px;
  margin-bottom: 4px;
  border: 1px solid transparent;
  border-radius: 8px;
  padding: 4px 6px;
  background: rgba(0, 0, 0, 0.12);
}
.meld-seat.me { border-color: rgba(232, 197, 71, 0.4); }
.meld-seat.turn { box-shadow: 0 0 0 1px #e8c547; }
.meld-group {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  margin-right: 8px;
  padding: 2px 4px;
  background: rgba(255, 255, 255, 0.06);
  border-radius: 4px;
}
.meld-group.dark { background: rgba(0, 0, 0, 0.28); }
.settle { background: rgba(0, 0, 0, 0.2); padding: 8px; border-radius: 8px; }
.logs {
  list-style: none;
  margin: 0;
  padding: 0;
  max-height: 120px;
  overflow: auto;
  font-size: 11px;
  color: #b7cfc2;
}
button {
  border: 0;
  background: rgba(255, 255, 255, 0.12);
  color: #e8f2ec;
  padding: 6px 10px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 12px;
}
button:disabled { opacity: 0.45; cursor: not-allowed; }
button.primary {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-weight: 700;
}
button.ghost { background: transparent; border: 1px solid rgba(255, 255, 255, 0.2); }
button.warn { background: #c2410c; color: #fff; font-weight: 700; }
</style>