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
  get wsOk() {
    return c.wsOk.value
  },
  get roomId() {
    return c.roomId.value
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
      <button v-else-if="c.canReady.value && !c.iAmReady.value" @click="c.doReady()">准备</button>
      <span v-else-if="c.matching.value" class="muted">匹配中…</span>
      <span v-else-if="c.iAmReady.value" class="muted">已准备</span>
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
      <span class="tile" :class="{ red: phzIsRed(c.phzReveal.value.tile) }">{{
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
            class="tile sm"
            :class="{ red: phzIsRed(t) }"
            >{{ c.phzTileLabel(t) }}</span
          >
        </span>
      </div>
    </div>
    <div class="hand">
      <button
        v-for="(t, i) in c.hand.value"
        :key="i + '-' + t"
        class="tile"
        :class="{ on: c.selectedHandIndex.value === i, red: phzIsRed(t) }"
        @click="c.selectTile(i)"
      >
        {{ c.phzTileLabel(t) }}
      </button>
    </div>
    <div class="row ops" v-if="c.isDiscardTurn.value || c.isClaimTurn.value">
      <button v-if="c.isDiscardTurn.value" :disabled="c.selectedHandIndex.value == null" @click="c.discardSelected()">
        出牌
      </button>
      <template v-if="c.isClaimTurn.value">
        <button @click="c.claim(0)">过</button>
        <button v-if="c.canClaimChi.value" @click="c.claim(1)">吃</button>
        <button v-if="c.canClaimPeng.value" @click="c.claim(2)">碰</button>
        <button v-if="c.phzCanHu.value" @click="c.claim(4)">胡</button>
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
  border: 1px solid #c5cdd8;
  border-radius: 10px;
  padding: 10px;
  background: #f7f9fc;
  min-height: 320px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.panel.active {
  border-color: #f59e0b;
  box-shadow: 0 0 0 2px #f59e0b;
}
.panel.claim {
  border-color: #0f766e;
  box-shadow: 0 0 0 2px #0f766e;
}
.cd {
  background: #fef3c7;
  color: #b45309;
  padding: 4px 8px;
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
.muted {
  color: #6b7785;
  font-size: 12px;
}
.row {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  align-items: center;
}
.err {
  color: #b42318;
  margin: 0;
  font-size: 13px;
}
.meta {
  font-size: 12px;
  color: #344054;
}
.reveal {
  padding: 6px 8px;
  background: #fff7ed;
  border-radius: 6px;
}
.hand {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  min-height: 40px;
}
.tile {
  min-width: 32px;
  padding: 6px 8px;
  border: 1px solid #98a2b3;
  border-radius: 6px;
  background: #fff;
  cursor: pointer;
  font-weight: 600;
}
.tile.sm {
  min-width: 24px;
  padding: 2px 4px;
  font-size: 12px;
  cursor: default;
}
.tile.on {
  outline: 2px solid #1570ef;
}
.tile.red {
  color: #d92d20;
}
.meld-seat {
  font-size: 12px;
  margin-bottom: 4px;
  border: 1px solid transparent;
  border-radius: 6px;
  padding: 2px 4px;
}
.meld-seat.me {
  border-color: #0f766e;
  background: #f0fdfa;
}
.meld-seat.turn {
  box-shadow: 0 0 0 1px #f59e0b;
}
.meld-group {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  margin-right: 8px;
  padding: 2px 4px;
  background: #eef2f6;
  border-radius: 4px;
}
.meld-group.dark {
  background: #d0d5dd;
}
.settle {
  background: #ecfdf3;
  padding: 8px;
  border-radius: 8px;
}
.logs {
  list-style: none;
  margin: 0;
  padding: 0;
  max-height: 120px;
  overflow: auto;
  font-size: 11px;
  color: #475467;
}
button {
  cursor: pointer;
}
</style>
