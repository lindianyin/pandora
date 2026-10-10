<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useGameSession } from '../composables/useGameSession'
import { ddzCardFace } from '../net/frame'

const router = useRouter()
const {
  uid,
  nickname,
  gold,
  diamond,
  wsOk,
  roomId,
  roomPhase,
  ddzRoundId,
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
} = useGameSession()

onMounted(() => {
  if (!wsOk.value) router.replace('/login')
})

function seatLabel(seatId: number | undefined) {
  if (seatId === undefined) return '-'
  return cardsLeft.value[seatId] ?? '-'
}

function face(id: number) {
  return ddzCardFace(id)
}
</script>

<template>
  <div class="felt">
    <header class="topbar">
      <div>
        <h1>斗地主</h1>
        <p>
          {{ nickname }} · 金币 {{ gold }} · 房间 #{{ roomId || '-' }}
          <span v-if="ddzRoundId"> · 局 {{ ddzRoundId }}</span>
          · {{ roomPhase || turnPhase || '-' }}
          · WSS {{ wsOk ? '已连接' : '断开' }}
        </p>
      </div>
      <div class="top-actions">
        <span v-if="countdown > 0 && turnPhase" class="timer" :class="{ mine: isMyTurn }">
          {{ isMyTurn ? '你的回合' : `座位${turnSeat}` }} · {{ turnPhase }} · {{ countdown }}s
        </span>
        <button class="ghost" @click="backLobby">大厅</button>
        <button class="ghost" @click="router.push('/ddz-lab')">三联 Lab</button>
      </div>
    </header>

    <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>
    <div v-if="reconnectHint" class="banner ok">{{ reconnectHint }}</div>

    <div class="arena">
      <div class="side left">
        <div v-if="seatLeft" class="seat" :class="{ turn: seatLeft.seat_id === turnSeat }">
          <div class="name">
            {{ seatLeft.nickname }}
            <span v-if="seatLeft.seat_id === landlordSeat" class="tag">地主</span>
          </div>
          <div class="info">
            {{ seatLeft.ready ? '已准备' : '未准备' }} · 剩{{ seatLabel(seatLeft.seat_id) }}张
            <span v-if="seatLeft.trusteeship" class="trust">托管</span>
            <span v-else-if="!seatLeft.online" class="trust">离线</span>
          </div>
          <div class="play">{{ lastPlays[seatLeft.seat_id] || ' ' }}</div>
        </div>
      </div>

      <div class="center">
        <div v-if="bottom.length" class="bottom">
          <span class="bottom-label">底牌</span>
          <button
            v-for="c in bottom"
            :key="'b' + c"
            type="button"
            class="pcard sm"
            :class="{ red: face(c).red, joker: face(c).joker }"
            disabled
          >
            <span class="rank">{{ face(c).joker ? (face(c).red ? '大' : '小') : face(c).rank }}</span>
            <span class="suit">{{ face(c).joker ? '王' : face(c).suit }}</span>
          </button>
        </div>
        <div v-else class="bottom muted">等待开局</div>
      </div>

      <div class="side right">
        <div v-if="seatRight" class="seat" :class="{ turn: seatRight.seat_id === turnSeat }">
          <div class="name">
            {{ seatRight.nickname }}
            <span v-if="seatRight.seat_id === landlordSeat" class="tag">地主</span>
          </div>
          <div class="info">
            {{ seatRight.ready ? '已准备' : '未准备' }} · 剩{{ seatLabel(seatRight.seat_id) }}张
            <span v-if="seatRight.trusteeship" class="trust">托管</span>
            <span v-else-if="!seatRight.online" class="trust">离线</span>
          </div>
          <div class="play">{{ lastPlays[seatRight.seat_id] || ' ' }}</div>
        </div>
      </div>

      <div class="self">
        <div v-if="seatSelf" class="seat me" :class="{ turn: seatSelf.seat_id === turnSeat }">
          <div class="name">
            {{ seatSelf.nickname }}（我）
            <span v-if="seatSelf.seat_id === landlordSeat" class="tag">地主</span>
          </div>
          <div class="info">
            {{ seatSelf.ready ? '已准备' : '未准备' }} · 剩{{ seatLabel(seatSelf.seat_id) }}张
            <span v-if="seatSelf.trusteeship" class="trust">托管</span>
          </div>
          <div class="play">{{ lastPlays[seatSelf.seat_id] || ' ' }}</div>
        </div>

        <div class="hand">
          <button
            v-for="c in hand"
            :key="c + '-' + selected.includes(c)"
            type="button"
            class="pcard"
            :class="{ on: selected.includes(c), red: face(c).red, joker: face(c).joker }"
            @click="toggleCard(c)"
          >
            <span class="rank">{{ face(c).joker ? (face(c).red ? '大' : '小') : face(c).rank }}</span>
            <span class="suit">{{ face(c).joker ? '王' : face(c).suit }}</span>
          </button>
        </div>

        <div class="actions">
          <button v-if="canReady" class="primary" :disabled="iAmReady" @click="doReady">
            {{ iAmReady ? '已准备' : '准备' }}
          </button>
          <template v-if="turnPhase === 'Bid' && isMyTurn">
            <button @click="doBid(0)">不叫</button>
            <button class="primary" @click="doBid(1)">1分</button>
            <button class="primary" @click="doBid(2)">2分</button>
            <button class="primary" @click="doBid(3)">3分</button>
          </template>
          <template v-if="turnPhase === 'Play' && isMyTurn">
            <button class="primary" :disabled="!selected.length" @click="doPlay">出牌</button>
            <button @click="doPass">过</button>
          </template>
        </div>
      </div>
    </div>

    <details class="log-box">
      <summary>日志</summary>
      <pre>{{ logs.join('\n') }}</pre>
    </details>

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
        <div class="actions">
          <button class="primary" @click="doReady">再准备</button>
          <button class="ghost-dark" @click="backLobby">回大厅</button>
          <button class="ghost-dark" @click="closeSettleStay">关闭</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.felt {
  min-height: 100vh;
  padding: 16px 18px 28px;
  background:
    radial-gradient(ellipse at top, rgba(50, 100, 70, 0.5), transparent 55%),
    linear-gradient(180deg, #123226 0%, #0b1a14 100%);
  color: #e8f2ec;
}
.topbar {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
  margin-bottom: 12px;
}
.topbar h1 { margin: 0; font-size: 1.5rem; letter-spacing: 0.04em; }
.topbar p { margin: 4px 0 0; opacity: 0.8; font-size: 13px; }
.top-actions { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; }
.timer {
  background: rgba(0, 0, 0, 0.28);
  border: 1px solid rgba(232, 197, 71, 0.35);
  color: #e8c547;
  border-radius: 999px;
  padding: 6px 12px;
  font-size: 13px;
}
.timer.mine { border-color: #e8c547; background: rgba(232, 197, 71, 0.15); }
.banner { padding: 8px 12px; border-radius: 8px; margin-bottom: 10px; font-size: 13px; }
.banner.err { background: rgba(185, 28, 28, 0.25); color: #ffb4b4; }
.banner.ok { background: rgba(4, 120, 87, 0.25); color: #9be7c4; }
.arena {
  display: grid;
  grid-template-columns: 1fr 1.3fr 1fr;
  grid-template-rows: auto auto;
  gap: 12px;
  max-width: 1100px;
  margin: 0 auto;
}
.side.left { grid-column: 1; grid-row: 1; }
.center { grid-column: 2; grid-row: 1; display: flex; align-items: center; justify-content: center; }
.side.right { grid-column: 3; grid-row: 1; }
.self { grid-column: 1 / 4; grid-row: 2; }
.seat {
  background: rgba(0, 0, 0, 0.22);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 14px;
  padding: 12px;
  min-height: 84px;
}
.seat.me { border-color: rgba(232, 197, 71, 0.45); background: rgba(232, 197, 71, 0.08); }
.seat.turn { box-shadow: 0 0 0 2px #e8c547; }
.name { font-weight: 700; }
.info { margin-top: 4px; font-size: 12px; color: #9db5a8; }
.tag {
  margin-left: 6px;
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-size: 11px;
  padding: 2px 7px;
  border-radius: 4px;
  font-weight: 700;
}
.trust { color: #ffb074; margin-left: 6px; }
.play { margin-top: 8px; min-height: 1.3em; font-weight: 700; color: #f3f7f4; }
.bottom { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; justify-content: center; }
.bottom-label { font-size: 12px; color: #9db5a8; margin-right: 4px; }
.bottom.muted { color: #6f877a; }
.hand {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin: 14px 0 10px;
  min-height: 84px;
  justify-content: center;
}
.pcard {
  width: 52px;
  height: 74px;
  border: 0;
  border-radius: 8px;
  background: linear-gradient(180deg, #fffef8 0%, #f2efe6 100%);
  color: #1a1a1a;
  box-shadow: 0 2px 0 #c9c2b0, 0 8px 16px rgba(0, 0, 0, 0.28);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  cursor: pointer;
  padding: 0;
  transition: transform 0.12s;
}
.pcard.sm { width: 40px; height: 58px; cursor: default; }
.pcard:hover:not(:disabled) { transform: translateY(-4px); }
.pcard.red { color: #c62828; }
.pcard.joker { color: #1d4ed8; }
.pcard.joker.red { color: #c62828; }
.pcard.on {
  outline: 2px solid #e8c547;
  outline-offset: 2px;
  transform: translateY(-10px);
}
.pcard .rank { font-size: 15px; font-weight: 800; line-height: 1; }
.pcard .suit { font-size: 18px; line-height: 1; }
.pcard.sm .rank { font-size: 12px; }
.pcard.sm .suit { font-size: 14px; }
.actions { display: flex; flex-wrap: wrap; gap: 8px; justify-content: center; }
.actions button, .top-actions button, .ghost {
  border: 0;
  border-radius: 8px;
  padding: 8px 14px;
  background: rgba(255, 255, 255, 0.12);
  color: #e8f2ec;
  cursor: pointer;
}
.actions button:disabled { opacity: 0.4; cursor: not-allowed; }
.actions .primary, .top-actions .primary {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-weight: 700;
}
.ghost, .top-actions .ghost {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.22);
}
.ghost-dark {
  border: 1px solid #cbd5e1;
  background: #f8fafc;
  color: #334155;
  border-radius: 8px;
  padding: 8px 14px;
  cursor: pointer;
}
.log-box {
  max-width: 1100px;
  margin: 16px auto 0;
  background: rgba(0, 0, 0, 0.22);
  border-radius: 10px;
  padding: 8px 12px;
  color: #b7cfc2;
  font-size: 12px;
}
.log-box summary { cursor: pointer; }
.log-box pre {
  margin: 8px 0 0;
  max-height: 160px;
  overflow: auto;
  white-space: pre-wrap;
}
.modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 50;
}
.modal {
  background: #fff;
  color: #1a1a1a;
  border-radius: 14px;
  padding: 20px;
  width: min(400px, 92vw);
}
.modal h3 { margin: 0 0 8px; }
.modal ul { padding-left: 18px; }
.win { color: #15803d; font-weight: 700; }
.lose { color: #b91c1c; font-weight: 700; }
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