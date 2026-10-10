<script setup lang="ts">
import { computed, onBeforeUnmount } from 'vue'
import { createDdzLabClient } from '../composables/createDdzLabClient'
import { ddzCardFace } from '../net/frame'

const props = defineProps<{ slotIndex: number; deviceId: string }>()
const c = createDdzLabClient(props.slotIndex, props.deviceId)

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

function face(id: number) {
  return ddzCardFace(id)
}
</script>

<template>
  <section
    class="panel"
    :class="[`pos-s${tablePos}`, { active: c.isBidTurn.value || c.isPlayTurn.value }]"
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
      <button v-if="c.canReady.value" class="primary" :disabled="c.iAmReady.value" @click="c.doReady()">
        {{ c.iAmReady.value ? 'OK' : '准备' }}
      </button>
      <button v-if="c.roomId.value" class="ghost" @click="c.leave()">离开</button>
    </div>

    <div v-if="c.countdown.value > 0 && c.turnPhase.value" class="cd">
      {{ c.isMyTurn.value ? '你的回合' : '座' + c.turnSeat.value }}
      · {{ c.turnPhase.value }} · {{ c.countdown.value }}s
    </div>

    <div v-if="c.bottom.value.length" class="bottom">
      <span class="meta">底牌</span>
      <button
        v-for="card in c.bottom.value"
        :key="'b' + card"
        type="button"
        class="pcard sm"
        :class="{ red: face(card).red, joker: face(card).joker }"
        disabled
      >
        <span class="rank">{{ face(card).joker ? (face(card).red ? '大' : '小') : face(card).rank }}</span>
        <span class="suit">{{ face(card).joker ? '王' : face(card).suit }}</span>
      </button>
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
          <span v-if="c.landlordSeat.value >= 0 && s.seat_id === c.landlordSeat.value" class="tag">地主</span>
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
        type="button"
        class="pcard"
        :class="{ on: c.selected.value.includes(card), red: face(card).red, joker: face(card).joker }"
        @click="c.toggleCard(card)"
      >
        <span class="rank">{{ face(card).joker ? (face(card).red ? '大' : '小') : face(card).rank }}</span>
        <span class="suit">{{ face(card).joker ? '王' : face(card).suit }}</span>
      </button>
    </div>

    <div class="row">
      <template v-if="c.isBidTurn.value">
        <button @click="c.doBid(0)">不叫</button>
        <button class="primary" @click="c.doBid(1)">1分</button>
        <button class="primary" @click="c.doBid(2)">2分</button>
        <button class="primary" @click="c.doBid(3)">3分</button>
      </template>
      <template v-if="c.isPlayTurn.value">
        <button class="primary" :disabled="!c.selected.value.length" @click="c.doPlay()">出牌</button>
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
  background: linear-gradient(160deg, #14523c 0%, #0f3d2e 45%, #0a2a20 100%);
  color: #e8f2ec;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 14px;
  padding: 12px;
  font-size: 12px;
  box-shadow: 0 10px 24px rgba(0, 0, 0, 0.28);
}
.panel.active { box-shadow: 0 0 0 2px #e8c547, 0 10px 24px rgba(0, 0, 0, 0.28); }
.ph { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 6px; }
.pos {
  background: #e8c547;
  color: #2a2108;
  padding: 1px 7px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 700;
}
.dev {
  color: #9db5a8;
  font-family: ui-monospace, Consolas, monospace;
  max-width: 160px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.meta { color: #9db5a8; }
.tag {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 700;
}
.trust { color: #ffb074; margin-left: 4px; }
.err { background: rgba(185, 28, 28, 0.25); color: #ffb4b4; padding: 4px 8px; border-radius: 6px; margin-bottom: 6px; }
.row { display: flex; flex-wrap: wrap; gap: 6px; margin: 6px 0; }
.cd {
  background: rgba(232, 197, 71, 0.15);
  color: #e8c547;
  border: 1px solid rgba(232, 197, 71, 0.35);
  padding: 4px 10px;
  border-radius: 999px;
  display: inline-block;
  margin-bottom: 6px;
}
.bottom { display: flex; flex-wrap: wrap; gap: 4px; align-items: center; margin-bottom: 6px; }
.seats { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 6px; margin-bottom: 6px; }
.mini {
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 8px;
  padding: 6px;
  background: rgba(0, 0, 0, 0.18);
}
.mini.me { border-color: rgba(232, 197, 71, 0.45); }
.mini.turn { box-shadow: 0 0 0 1px #e8c547; }
.lp { min-height: 1em; font-weight: 700; margin-top: 2px; }
.hand-label { color: #9db5a8; margin-top: 4px; }
.hand { display: flex; flex-wrap: wrap; gap: 5px; min-height: 52px; }
.pcard {
  width: 40px;
  height: 56px;
  border: 0;
  border-radius: 6px;
  background: linear-gradient(180deg, #fffef8 0%, #f2efe6 100%);
  color: #1a1a1a;
  box-shadow: 0 2px 0 #c9c2b0, 0 4px 10px rgba(0, 0, 0, 0.25);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1px;
  cursor: pointer;
  padding: 0;
}
.pcard.sm { width: 32px; height: 44px; cursor: default; }
.pcard.red { color: #c62828; }
.pcard.joker { color: #1d4ed8; }
.pcard.joker.red { color: #c62828; }
.pcard.on { outline: 2px solid #e8c547; outline-offset: 1px; transform: translateY(-6px); }
.pcard .rank { font-size: 12px; font-weight: 800; line-height: 1; }
.pcard .suit { font-size: 14px; line-height: 1; }
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
.settle { background: rgba(0, 0, 0, 0.2); padding: 6px; border-radius: 6px; margin-top: 6px; }
.log {
  margin: 8px 0 0;
  max-height: 90px;
  overflow: auto;
  background: rgba(0, 0, 0, 0.28);
  color: #b7cfc2;
  padding: 6px;
  border-radius: 6px;
  font-size: 10px;
}
</style>