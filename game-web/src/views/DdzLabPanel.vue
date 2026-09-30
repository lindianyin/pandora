<script setup lang="ts">
import { computed, onBeforeUnmount } from 'vue'
import { createDdzLabClient } from '../composables/createDdzLabClient'

const props = defineProps<{ slotIndex: number; deviceId: string }>()
const c = createDdzLabClient(props.slotIndex, props.deviceId)

/** Table position: seat0 bottom, seat1 right, seat2 top */
const tablePos = computed(() => {
  const seat = c.mySeat.value >= 0 ? c.mySeat.value : props.slotIndex
  return Math.min(2, Math.max(0, seat))
})
const posLabel = computed(() => ['下', '右', '上'][tablePos.value] || '')

onBeforeUnmount(() => c.dispose())

defineExpose({
  login: () => c.login(),
  matchDdz: () => c.matchDdz(),
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
    :class="[
      `pos-s${tablePos}`,
      { active: c.isBidTurn.value || c.isPlayTurn.value },
    ]"
  >
    <div class="ph">
      <strong>{{ c.label }}</strong>
      <span class="pos">{{ posLabel }}</span>
      <span class="dev" :title="c.deviceId">{{ c.deviceId }}</span>
      <span v-if="c.uid.value">uid {{ c.uid.value }} {{ c.nickname.value }}</span>
      <span v-else>offline</span>
      <span v-if="c.mySeat.value >= 0">座{{ c.mySeat.value }}</span>
      <span v-if="c.landlordSeat.value >= 0 && c.mySeat.value === c.landlordSeat.value" class="tag">地主</span>
      <span v-if="c.ddzRoundId.value" class="tag">局 {{ c.ddzRoundId.value }}</span>
      <span class="meta">{{ c.roomPhase.value || c.turnPhase.value || '-' }}</span>
    </div>
    <div v-if="c.errorBanner.value" class="err">{{ c.errorBanner.value }}</div>
    <div class="row">
      <button v-if="!c.wsOk.value" :disabled="c.busy.value" @click="c.login()">登录</button>
      <button v-else-if="!c.roomId.value" :disabled="c.matching.value" @click="c.matchDdz()">匹配</button>
      <button v-if="c.canReady.value" :disabled="c.iAmReady.value" @click="c.doReady()">
        {{ c.iAmReady.value ? 'OK' : '准备' }}
      </button>
      <button v-if="c.roomId.value" class="ghost" @click="c.leave()">离开</button>
    </div>

    <div v-if="c.countdown.value > 0 && c.turnPhase.value" class="cd">
      {{ c.isMyTurn.value ? '你的回合' : '座' + c.turnSeat.value }}
      · {{ c.turnPhase.value }} · {{ c.countdown.value }}s
    </div>

    <div v-if="c.bottom.value.length" class="bottom">
      底牌 {{ c.bottom.value.map(c.cardLabel).join(' ') }}
    </div>

    <div class="seats">
      <div
        v-for="s in c.seats.value"
        :key="s.seat_id"
        class="mini"
        :class="{ me: s.seat_id === c.mySeat.value, turn: s.seat_id === c.turnSeat.value }"
      >
        <div>
          {{ s.nickname }} 座{{ s.seat_id }} 剩{{ c.cardsLeft.value[s.seat_id] ?? '-' }}
          <span v-if="c.landlordSeat.value >= 0 && s.seat_id === c.landlordSeat.value" class="tag mini-tag">地主</span>
          <span v-if="s.trusteeship" class="trust">托管</span>
          <span v-else-if="!s.online" class="trust">离线</span>
        </div>
        <div class="lp">{{ c.lastPlays.value[s.seat_id] || '' }}</div>
      </div>
    </div>

    <div class="hand-label">手牌</div>
    <div class="hand">
      <button
        v-for="card in c.hand.value"
        :key="card + '-' + c.selected.value.includes(card)"
        class="tile"
        :class="{ on: c.selected.value.includes(card) }"
        @click="c.toggleCard(card)"
      >
        {{ c.cardLabel(card) }}
      </button>
    </div>

    <div class="row">
      <template v-if="c.isBidTurn.value">
        <button @click="c.doBid(0)">不叫</button>
        <button @click="c.doBid(1)">1分</button>
        <button @click="c.doBid(2)">2分</button>
        <button @click="c.doBid(3)">3分</button>
      </template>
      <template v-if="c.isPlayTurn.value">
        <button :disabled="!c.selected.value.length" @click="c.doPlay()">出牌</button>
        <button @click="c.doPass()">过</button>
      </template>
    </div>

    <div v-if="c.showSettle.value && c.settle.value" class="settle">
      <strong>结算</strong>
      底分 {{ c.settle.value.base_score }} × {{ c.settle.value.multiplier }}
      <span v-for="e in c.settle.value.entries" :key="e.uid">
        座{{ e.seat_id }} {{ e.delta_gold >= 0 ? '+' : '' }}{{ e.delta_gold }}
        <em v-if="e.uid === c.uid.value">(我)</em>
      </span>
    </div>

    <pre class="log">{{ c.logs.value.slice(0, 8).join('\n') }}</pre>
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
.ph { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 6px; }
.pos {
  background: #1d4ed8;
  color: #fff;
  padding: 1px 7px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 600;
}
.dev {
  color: #64748b;
  font-family: ui-monospace, Consolas, monospace;
  max-width: 180px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.meta { color: #64748b; }
.tag { background: #b45309; color: #fff; padding: 1px 6px; border-radius: 4px; font-size: 11px; }
.mini-tag { margin-left: 4px; }
.trust { color: #c2410c; margin-left: 4px; }
.err { background: #fef2f2; color: #b91c1c; padding: 4px 8px; border-radius: 4px; margin-bottom: 6px; }
.row { display: flex; flex-wrap: wrap; gap: 6px; margin: 6px 0; }
.cd { background: #fef3c7; color: #b45309; padding: 4px 8px; border-radius: 999px; display: inline-block; margin-bottom: 6px; }
.bottom { font-weight: 600; margin-bottom: 6px; }
.seats { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 6px; margin-bottom: 6px; }
.mini { border: 1px solid #e2e8f0; border-radius: 6px; padding: 4px 6px; background: #f8fafc; }
.mini.me { border-color: #1d4ed8; background: #eff6ff; }
.mini.turn { box-shadow: 0 0 0 1px #f59e0b; }
.lp { min-height: 1em; font-weight: 600; margin-top: 2px; }
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
.tile.on { background: #1d4ed8; color: #fff; border-color: #1d4ed8; }
button {
  border: 0;
  background: #1d4ed8;
  color: #fff;
  padding: 6px 10px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 12px;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.ghost { background: #e2e8f0; color: #334155; }
.settle { background: #eff6ff; padding: 6px; border-radius: 6px; margin-top: 6px; }
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
