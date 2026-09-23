<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const {
  uid,
  nickname,
  gold,
  diamond,
  wsOk,
  roomId,
  roomPhase,
  landlordSeat,
  bottom,
  hand,
  selected,
  turnPhase,
  turnSeat,
  countdown,
  isMyTurn,
  iAmReady,
  canReady,
  seatLeft,
  seatRight,
  seatSelf,
  cardsLeft,
  lastPlays,
  settle,
  showSettle,
  errorBanner,
  reconnectHint,
  logs,
  doReady,
  doBid,
  toggleCard,
  doPlay,
  doPass,
  backLobby,
  closeSettleStay,
  cardLabel,
} = useGameSession()

onMounted(() => {
  if (!wsOk.value) router.replace('/login')
})

function seatLabel(seatId: number | undefined) {
  if (seatId === undefined) return '-'
  return cardsLeft.value[seatId] ?? '-'
}
</script>

<template>
  <section class="card profile">
    <div>UID: {{ uid }} · {{ nickname }} · 金币 {{ gold }} / 钻石 {{ diamond }}</div>
    <div>WSS: {{ wsOk ? '已鉴权' : '未鉴权' }} · 房间 #{{ roomId }} · {{ roomPhase || turnPhase || '-' }}</div>
  </section>

  <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>
  <div v-if="reconnectHint" class="banner ok">{{ reconnectHint }}</div>

  <section class="card table">
    <div class="table-head">
      <h2>牌桌</h2>
      <span v-if="countdown > 0 && turnPhase" class="cd" :class="{ mine: isMyTurn }">
        {{ isMyTurn ? '你的回合' : `座位${turnSeat}` }} · {{ turnPhase }} · {{ countdown }}s
      </span>
    </div>

    <div class="arena">
      <div class="side left">
        <div v-if="seatLeft" class="seat" :class="{ turn: seatLeft.seat_id === turnSeat }">
          <div class="name">{{ seatLeft.nickname }}</div>
          <div class="info">
            {{ seatLeft.ready ? '已准备' : '未准备' }} · 剩{{ seatLabel(seatLeft.seat_id) }}张
            <span v-if="seatLeft.trusteeship" class="trust">托管中</span>
            <span v-else-if="!seatLeft.online" class="trust">离线</span>
          </div>
          <div v-if="seatLeft.seat_id === landlordSeat" class="tag">地主</div>
          <div class="play">{{ lastPlays[seatLeft.seat_id] || '' }}</div>
        </div>
      </div>

      <div class="center">
        <div v-if="bottom.length" class="bottom">底牌 {{ bottom.map(cardLabel).join(' ') }}</div>
        <div v-else class="bottom muted">等待开局</div>
      </div>

      <div class="side right">
        <div v-if="seatRight" class="seat" :class="{ turn: seatRight.seat_id === turnSeat }">
          <div class="name">{{ seatRight.nickname }}</div>
          <div class="info">
            {{ seatRight.ready ? '已准备' : '未准备' }} · 剩{{ seatLabel(seatRight.seat_id) }}张
            <span v-if="seatRight.trusteeship" class="trust">托管中</span>
            <span v-else-if="!seatRight.online" class="trust">离线</span>
          </div>
          <div v-if="seatRight.seat_id === landlordSeat" class="tag">地主</div>
          <div class="play">{{ lastPlays[seatRight.seat_id] || '' }}</div>
        </div>
      </div>

      <div class="self">
        <div v-if="seatSelf" class="seat me" :class="{ turn: seatSelf.seat_id === turnSeat }">
          <div class="name">{{ seatSelf.nickname }}（我）</div>
          <div class="info">
            {{ seatSelf.ready ? '已准备' : '未准备' }} · 剩{{ seatLabel(seatSelf.seat_id) }}张
            <span v-if="seatSelf.trusteeship" class="trust">托管中</span>
          </div>
          <div v-if="seatSelf.seat_id === landlordSeat" class="tag">地主</div>
          <div class="play">{{ lastPlays[seatSelf.seat_id] || '' }}</div>
        </div>

        <div class="hand">
          <button
            v-for="c in hand"
            :key="c + '-' + selected.includes(c)"
            class="card-btn"
            :class="{ on: selected.includes(c) }"
            @click="toggleCard(c)"
          >
            {{ cardLabel(c) }}
          </button>
        </div>

        <div class="row actions">
          <button
            v-if="canReady"
            :disabled="iAmReady"
            @click="doReady"
          >
            {{ iAmReady ? '已准备' : '准备' }}
          </button>
          <template v-if="turnPhase === 'Bid' && isMyTurn">
            <button @click="doBid(0)">不叫</button>
            <button @click="doBid(1)">1分</button>
            <button @click="doBid(2)">2分</button>
            <button @click="doBid(3)">3分</button>
          </template>
          <template v-if="turnPhase === 'Play' && isMyTurn">
            <button :disabled="!selected.length" @click="doPlay">出牌</button>
            <button @click="doPass">过</button>
          </template>
          <button class="ghost" @click="backLobby">回大厅</button>
        </div>
      </div>
    </div>
  </section>

  <div v-if="showSettle && settle" class="modal-mask">
    <div class="modal">
      <h3>本局结算</h3>
      <p>底分 {{ settle.base_score }} × 倍数 {{ settle.multiplier }}</p>
      <ul>
        <li v-for="e in settle.entries" :key="e.uid">
          座位{{ e.seat_id }}
          <span :class="e.delta_gold >= 0 ? 'win' : 'lose'">
            {{ e.delta_gold >= 0 ? '+' : '' }}{{ e.delta_gold }}
          </span>
          <span v-if="e.uid === uid">（我）</span>
        </li>
      </ul>
      <div class="row">
        <button @click="doReady">再准备</button>
        <button class="ghost" @click="backLobby">回大厅</button>
        <button class="ghost" @click="closeSettleStay">关闭</button>
      </div>
    </div>
  </div>

  <section class="card">
    <h2>日志</h2>
    <pre class="log">{{ logs.join('\n') }}</pre>
  </section>
</template>

<style scoped>
.card {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 16px;
  margin-bottom: 16px;
}
.profile { line-height: 1.6; font-size: 14px; }
.table-head { display: flex; justify-content: space-between; align-items: center; gap: 12px; }
.table-head h2 { margin: 0; }
.cd {
  background: #f1f5f9;
  color: #475569;
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 13px;
}
.cd.mine { background: #fef3c7; color: #b45309; }
.arena {
  display: grid;
  grid-template-columns: 1fr 1.2fr 1fr;
  grid-template-rows: auto auto;
  gap: 12px;
  margin-top: 12px;
}
.side.left { grid-column: 1; grid-row: 1; }
.center { grid-column: 2; grid-row: 1; display: flex; align-items: center; justify-content: center; }
.side.right { grid-column: 3; grid-row: 1; }
.self { grid-column: 1 / 4; grid-row: 2; }
.seat {
  border: 1px solid #ddd;
  border-radius: 8px;
  padding: 10px;
  font-size: 13px;
  position: relative;
  min-height: 72px;
  background: #fafafa;
}
.seat.me { border-color: #2563eb; background: #eff6ff; }
.seat.turn { box-shadow: 0 0 0 2px #f59e0b; }
.tag {
  position: absolute;
  top: 6px;
  right: 6px;
  background: #b45309;
  color: #fff;
  font-size: 11px;
  padding: 2px 6px;
  border-radius: 4px;
}
.play { margin-top: 6px; color: #0f172a; font-weight: 600; min-height: 1.2em; }
.bottom { color: #334155; font-size: 14px; }
.bottom.muted { color: #94a3b8; }
.hand { display: flex; flex-wrap: wrap; gap: 6px; margin: 12px 0; min-height: 40px; }
.card-btn {
  background: #f8fafc;
  color: #111;
  border: 1px solid #cbd5e1;
  min-width: 48px;
  padding: 8px 10px;
  border-radius: 6px;
  cursor: pointer;
}
.card-btn.on { background: #2563eb; color: #fff; border-color: #2563eb; }
.row { display: flex; gap: 8px; flex-wrap: wrap; }
button {
  border: 0;
  background: #2563eb;
  color: #fff;
  padding: 8px 14px;
  border-radius: 6px;
  cursor: pointer;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.ghost { background: #e2e8f0; color: #334155; }
.banner {
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 12px;
}
.banner.err { background: #fef2f2; color: #b91c1c; }
.banner.ok { background: #ecfdf5; color: #065f46; }
.trust {
  display: inline-block;
  margin-left: 6px;
  padding: 1px 6px;
  border-radius: 4px;
  background: #fef3c7;
  color: #92400e;
  font-size: 11px;
}
.modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(15, 23, 42, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 50;
}
.modal {
  background: #fff;
  border-radius: 12px;
  padding: 20px;
  width: min(400px, 92vw);
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.18);
}
.modal h3 { margin: 0 0 8px; }
.modal ul { padding-left: 18px; }
.win { color: #15803d; font-weight: 600; }
.lose { color: #b91c1c; font-weight: 600; }
.log {
  margin: 0;
  max-height: 200px;
  overflow: auto;
  background: #0f172a;
  color: #e2e8f0;
  padding: 12px;
  border-radius: 8px;
  font-size: 12px;
}
@media (max-width: 720px) {
  .arena {
    grid-template-columns: 1fr 1fr;
    grid-template-rows: auto auto auto;
  }
  .side.left { grid-column: 1; grid-row: 1; }
  .side.right { grid-column: 2; grid-row: 1; }
  .center { grid-column: 1 / 3; grid-row: 2; }
  .self { grid-column: 1 / 3; grid-row: 3; }
}
</style>
