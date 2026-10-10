<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useGameSession } from '../composables/useGameSession'
import { meldKindLabel, type HzmjMeld } from '../net/hzmjFront'
import { hzmjTileFace } from '../net/frame'

const router = useRouter()
const {
  uid,
  nickname,
  gold,
  diamond,
  wsOk,
  roomId,
  roomPhase,
  hand,
  selectedHandIndex,
  turnSeat,
  countdown,
  isMyTurn,
  iAmReady,
  canReady,
  seatLeft,
  seatRight,
  seatOpposite,
  seatSelf,
  cardsLeft,
  lastPlays,
  errorBanner,
  reconnectHint,
  logs,
  hzmjCaishen,
  hzmjBanker,
  hzmjLian,
  hzmjN,
  hzmjRoundId,
  hzmjWall,
  hzmjSub,
  hzmjPiaoSeat,
  hzmjLastDiscard,
  hzmjMelds,
  hzmjRivers,
  hzmjSettle,
  showHzmjSettle,
  showLiuJu,
  hzmjBaseScore,
  isHzmjClaim,
  isHzmjDiscardTurn,
  anGangCandidates,
  buGangCandidates,
  canClaimChi,
  canClaimPeng,
  canClaimGang,
  canClaimHu,
  canZimoHu,
  hzmjClaimSent,
  hzmjClaimHint,
  doReady,
  selectTile,
  doHzmjDiscard,
  doHzmjAction,
  doHzmjAnGang,
  doHzmjBuGang,
  backLobby,
  closeSettleStay,
  tileLabel,
} = useGameSession()

onMounted(() => {
  if (!wsOk.value) router.replace('/login')
})

function seatCards(seatId: number | undefined) {
  if (seatId === undefined) return '-'
  return cardsLeft.value[seatId] ?? '-'
}

function seatMelds(seatId: number | undefined): HzmjMeld[] {
  if (seatId === undefined) return []
  return hzmjMelds.value[seatId] || []
}

function seatRiver(seatId: number | undefined): number[] {
  if (seatId === undefined) return []
  return hzmjRivers.value[seatId] || []
}

function isCaishen(t: number) {
  return hzmjCaishen.value.includes(t)
}

function face(t: number) {
  return hzmjTileFace(t)
}

function faceText(m: HzmjMeld, t: number) {
  return m.kind === 4 ? '?' : tileLabel(t)
}
</script>

<template>
  <div class="felt">
    <header class="topbar">
      <div>
        <h1>杭州麻将</h1>
        <p>
          {{ nickname }} · 金币 {{ gold }} · 房间 #{{ roomId || '-' }} · 余牌 {{ hzmjWall }} · N={{ hzmjN }}
          · 局 {{ hzmjRoundId || '-' }} · 连庄 {{ hzmjLian }}
        </p>
      </div>
      <div class="top-actions">
        <span class="meta-pill">
          财神
          <span v-for="tile in hzmjCaishen" :key="tile" class="cai-chip">{{ tileLabel(tile) }}</span>
          · 庄 {{ hzmjBanker }}
          <span v-if="hzmjPiaoSeat >= 0"> · 财飘 seat{{ hzmjPiaoSeat }}</span>
        </span>
        <span v-if="countdown > 0 && hzmjSub" class="timer" :class="{ mine: isMyTurn || isHzmjClaim }">
          {{ isHzmjClaim ? '请选择' : isMyTurn ? '请出牌' : `等待${turnSeat}` }}
          · {{ hzmjSub }} · {{ countdown }}s
        </span>
        <button class="ghost" @click="backLobby">大厅</button>
        <button class="ghost" @click="router.push('/hzmj-lab')">四联 Lab</button>
      </div>
    </header>

    <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>
    <div v-if="reconnectHint" class="banner ok">{{ reconnectHint }}</div>

    <div class="arena">
      <div class="opp">
        <div v-if="seatOpposite" class="seat" :class="{ turn: seatOpposite.seat_id === turnSeat }">
          <div class="name">
            {{ seatOpposite.nickname }}
            <span v-if="seatOpposite.seat_id === hzmjBanker" class="tag">庄</span>
            <span class="info">{{ seatCards(seatOpposite.seat_id) }}张</span>
            <span v-if="seatOpposite.trusteeship" class="trust">托管</span>
          </div>
          <div class="front">
            <div v-if="seatMelds(seatOpposite.seat_id).length" class="meld-row">
              <div v-for="(m, mi) in seatMelds(seatOpposite.seat_id)" :key="'om' + mi" class="meld-group">
                <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                <span
                  v-for="(tile, ti) in m.tiles"
                  :key="'omt' + ti"
                  class="mtile sm"
                  :class="[m.kind === 4 ? 'an' : face(tile).suit, { cai: isCaishen(tile) }]"
                >
                  {{ faceText(m, tile) }}
                </span>
              </div>
            </div>
            <div v-if="seatRiver(seatOpposite.seat_id).length" class="river">
              <span
                v-for="(tile, ti) in seatRiver(seatOpposite.seat_id)"
                :key="'or' + ti"
                class="mtile xs"
                :class="[face(tile).suit, { cai: isCaishen(tile) }]"
              >
                {{ tileLabel(tile) }}
              </span>
            </div>
          </div>
          <div class="play">{{ lastPlays[seatOpposite.seat_id] || '' }}</div>
        </div>
      </div>

      <div class="mid">
        <div class="side left">
          <div v-if="seatLeft" class="seat" :class="{ turn: seatLeft.seat_id === turnSeat }">
            <div class="name">
              {{ seatLeft.nickname }}
              <span v-if="seatLeft.seat_id === hzmjBanker" class="tag">庄</span>
              <span class="info">{{ seatCards(seatLeft.seat_id) }}张</span>
            </div>
            <div class="front">
              <div v-if="seatMelds(seatLeft.seat_id).length" class="meld-row">
                <div v-for="(m, mi) in seatMelds(seatLeft.seat_id)" :key="'lm' + mi" class="meld-group">
                  <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                  <span
                    v-for="(tile, ti) in m.tiles"
                    :key="'lmt' + ti"
                    class="mtile sm"
                    :class="[m.kind === 4 ? 'an' : face(tile).suit, { cai: isCaishen(tile) }]"
                  >
                    {{ faceText(m, tile) }}
                  </span>
                </div>
              </div>
              <div v-if="seatRiver(seatLeft.seat_id).length" class="river">
                <span
                  v-for="(tile, ti) in seatRiver(seatLeft.seat_id)"
                  :key="'lr' + ti"
                  class="mtile xs"
                  :class="[face(tile).suit, { cai: isCaishen(tile) }]"
                >
                  {{ tileLabel(tile) }}
                </span>
              </div>
            </div>
            <div class="play">{{ lastPlays[seatLeft.seat_id] || '' }}</div>
          </div>
        </div>

        <div class="center">
          <div v-if="hzmjLastDiscard" class="discard">
            <span class="muted">seat{{ hzmjLastDiscard.seat }} 打出</span>
            <span class="mtile" :class="[face(hzmjLastDiscard.tile).suit, { cai: isCaishen(hzmjLastDiscard.tile) }]">
              <span class="num">{{ face(hzmjLastDiscard.tile).num }}</span>
              <span class="kind">{{ face(hzmjLastDiscard.tile).kind }}</span>
            </span>
          </div>
          <div v-else class="discard muted">等待出牌</div>
        </div>

        <div class="side right">
          <div v-if="seatRight" class="seat" :class="{ turn: seatRight.seat_id === turnSeat }">
            <div class="name">
              {{ seatRight.nickname }}
              <span v-if="seatRight.seat_id === hzmjBanker" class="tag">庄</span>
              <span class="info">{{ seatCards(seatRight.seat_id) }}张</span>
            </div>
            <div class="front">
              <div v-if="seatMelds(seatRight.seat_id).length" class="meld-row">
                <div v-for="(m, mi) in seatMelds(seatRight.seat_id)" :key="'rm' + mi" class="meld-group">
                  <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                  <span
                    v-for="(tile, ti) in m.tiles"
                    :key="'rmt' + ti"
                    class="mtile sm"
                    :class="[m.kind === 4 ? 'an' : face(tile).suit, { cai: isCaishen(tile) }]"
                  >
                    {{ faceText(m, tile) }}
                  </span>
                </div>
              </div>
              <div v-if="seatRiver(seatRight.seat_id).length" class="river">
                <span
                  v-for="(tile, ti) in seatRiver(seatRight.seat_id)"
                  :key="'rr' + ti"
                  class="mtile xs"
                  :class="[face(tile).suit, { cai: isCaishen(tile) }]"
                >
                  {{ tileLabel(tile) }}
                </span>
              </div>
            </div>
            <div class="play">{{ lastPlays[seatRight.seat_id] || '' }}</div>
          </div>
        </div>
      </div>

      <div class="self">
        <div v-if="seatSelf" class="seat me" :class="{ turn: seatSelf.seat_id === turnSeat }">
          <div class="name">
            {{ seatSelf.nickname }}（我）
            <span v-if="seatSelf.seat_id === hzmjBanker" class="tag">庄</span>
            <span class="info">
              {{ seatSelf.ready ? '已准备' : '未准备' }} · {{ seatCards(seatSelf.seat_id) }}张
            </span>
            <span v-if="seatSelf.trusteeship" class="trust">托管</span>
          </div>
          <div class="front">
            <div v-if="seatMelds(seatSelf.seat_id).length" class="meld-row">
              <div v-for="(m, mi) in seatMelds(seatSelf.seat_id)" :key="'sm' + mi" class="meld-group">
                <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                <span
                  v-for="(tile, ti) in m.tiles"
                  :key="'smt' + ti"
                  class="mtile sm"
                  :class="[m.kind === 4 ? 'an' : face(tile).suit, { cai: isCaishen(tile) }]"
                >
                  {{ faceText(m, tile) }}
                </span>
              </div>
            </div>
            <div v-if="seatRiver(seatSelf.seat_id).length" class="river">
              <span
                v-for="(tile, ti) in seatRiver(seatSelf.seat_id)"
                :key="'sr' + ti"
                class="mtile xs"
                :class="[face(tile).suit, { cai: isCaishen(tile) }]"
              >
                {{ tileLabel(tile) }}
              </span>
            </div>
          </div>
        </div>

        <div class="hand">
          <button
            v-for="(tile, idx) in hand"
            :key="idx + '-' + tile"
            type="button"
            class="mtile hand-tile"
            :class="[face(tile).suit, { on: selectedHandIndex === idx, cai: isCaishen(tile) }]"
            @click="selectTile(idx)"
          >
            <span class="num">{{ face(tile).num }}</span>
            <span class="kind">{{ face(tile).kind }}</span>
          </button>
        </div>

        <div class="actions">
          <button v-if="canReady" class="primary" :disabled="iAmReady" @click="doReady">
            {{ iAmReady ? '已准备' : '准备' }}
          </button>
          <template v-if="isHzmjDiscardTurn">
            <button v-if="canZimoHu" class="warn" @click="doHzmjAction(4)">自摸</button>
            <button class="primary" :disabled="selectedHandIndex == null" @click="doHzmjDiscard">出牌</button>
            <button v-for="g in anGangCandidates" :key="'gang-' + g" class="warn" @click="doHzmjAnGang(g)">
              暗杠 {{ tileLabel(g) }}
            </button>
            <button v-for="g in buGangCandidates" :key="'bugang-' + g" class="warn" @click="doHzmjBuGang(g)">
              补杠 {{ tileLabel(g) }}
            </button>
          </template>
          <template v-if="isHzmjClaim">
            <span v-if="hzmjClaimHint" class="hint">{{ hzmjClaimHint }}</span>
            <button :disabled="hzmjClaimSent" @click="doHzmjAction(0)">过</button>
            <button v-if="canClaimChi" :disabled="hzmjClaimSent" @click="doHzmjAction(1)">吃</button>
            <button v-if="canClaimPeng" :disabled="hzmjClaimSent" @click="doHzmjAction(2)">碰</button>
            <button v-if="canClaimGang" :disabled="hzmjClaimSent" @click="doHzmjAction(3)">杠</button>
            <button v-if="canClaimHu" class="warn" :disabled="hzmjClaimSent" @click="doHzmjAction(4)">胡</button>
          </template>
        </div>
      </div>
    </div>

    <details class="log-box">
      <summary>日志</summary>
      <pre>{{ logs.join('\n') }}</pre>
    </details>

    <div v-if="showHzmjSettle && hzmjSettle" class="modal-mask">
      <div class="modal">
        <h3>结算</h3>
        <p>
          底分 {{ hzmjSettle.base_score || hzmjBaseScore }} · M={{ hzmjSettle.M }} · N={{ hzmjSettle.N }}
          · {{ hzmjSettle.is_zimo ? '自摸' : '点炮' }}
          <span v-if="hzmjSettle.contractor_seat >= 0"> · 承包 seat{{ hzmjSettle.contractor_seat }}</span>
        </p>
        <ul>
          <li v-for="e in hzmjSettle.entries" :key="e.uid">
            座位{{ e.seat_id }}
            <span :class="e.delta_gold >= 0 ? 'win' : 'lose'">
              {{ e.delta_gold >= 0 ? '+' : '' }}{{ e.delta_gold }}
            </span>
            <span v-if="e.uid === uid">（我）</span>
          </li>
        </ul>
        <div class="actions">
          <button class="primary-dark" @click="doReady">再来一局</button>
          <button class="ghost-dark" @click="backLobby">返回大厅</button>
          <button class="ghost-dark" @click="closeSettleStay">关闭</button>
        </div>
      </div>
    </div>

    <div v-if="showLiuJu" class="modal-mask">
      <div class="modal">
        <h3>流局</h3>
        <p>连庄 {{ hzmjLian }}</p>
        <div class="actions">
          <button class="primary-dark" @click="doReady">再来一局</button>
          <button class="ghost-dark" @click="backLobby">返回大厅</button>
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
    radial-gradient(ellipse at top, rgba(40, 95, 75, 0.55), transparent 55%),
    linear-gradient(180deg, #0f2e24 0%, #0a1a14 100%);
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
.meta-pill, .timer {
  background: rgba(0, 0, 0, 0.28);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 999px;
  padding: 6px 12px;
  font-size: 12px;
  color: #cfe7da;
}
.timer { border-color: rgba(232, 197, 71, 0.35); color: #e8c547; }
.timer.mine { background: rgba(232, 197, 71, 0.15); }
.cai-chip {
  display: inline-block;
  margin-left: 4px;
  background: rgba(245, 158, 11, 0.25);
  color: #fbbf24;
  padding: 1px 6px;
  border-radius: 4px;
  font-weight: 700;
}
.banner { padding: 8px 12px; border-radius: 8px; margin-bottom: 10px; font-size: 13px; }
.banner.err { background: rgba(185, 28, 28, 0.25); color: #ffb4b4; }
.banner.ok { background: rgba(4, 120, 87, 0.25); color: #9be7c4; }
.arena {
  max-width: 1180px;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.opp { display: flex; justify-content: center; }
.mid {
  display: grid;
  grid-template-columns: 1fr 1.1fr 1fr;
  gap: 12px;
  align-items: center;
}
.center { text-align: center; }
.discard {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  background: rgba(0, 0, 0, 0.22);
  border-radius: 14px;
  padding: 12px 16px;
}
.discard.muted, .muted { color: #6f877a; }
.seat {
  background: rgba(0, 0, 0, 0.22);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 14px;
  padding: 12px;
  min-height: 72px;
}
.seat.me { border-color: rgba(232, 197, 71, 0.4); background: rgba(232, 197, 71, 0.08); }
.seat.turn { box-shadow: 0 0 0 2px #e8c547; }
.name { font-weight: 700; display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
.info { font-size: 12px; color: #9db5a8; font-weight: 400; }
.tag {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-size: 11px;
  padding: 2px 7px;
  border-radius: 4px;
  font-weight: 700;
}
.trust { color: #ffb074; }
.front { margin-top: 8px; display: flex; flex-direction: column; gap: 6px; }
.meld-row, .river { display: flex; flex-wrap: wrap; gap: 6px; }
.meld-group {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  padding: 3px 5px;
  background: rgba(255, 255, 255, 0.06);
  border-radius: 8px;
}
.meld-tag { font-size: 10px; color: #9db5a8; font-weight: 700; }
.play { margin-top: 6px; min-height: 1.2em; font-weight: 700; }
.hand {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin: 14px 0 10px;
  justify-content: center;
  min-height: 84px;
}
.mtile {
  display: inline-flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-width: 42px;
  height: 58px;
  padding: 0 6px;
  border: 0;
  border-radius: 8px;
  background: linear-gradient(180deg, #fffef8 0%, #efe9da 100%);
  box-shadow: 0 2px 0 #c4bba6, 0 6px 12px rgba(0, 0, 0, 0.28);
  color: #1a1a1a;
  font-weight: 700;
  line-height: 1.05;
}
.mtile.hand-tile {
  width: 52px;
  height: 74px;
  cursor: pointer;
  transition: transform 0.12s;
}
.mtile.hand-tile:hover { transform: translateY(-4px); }
.mtile.hand-tile.on {
  outline: 2px solid #e8c547;
  outline-offset: 2px;
  transform: translateY(-10px);
}
.mtile.sm { min-width: 34px; height: 44px; font-size: 12px; }
.mtile.xs { min-width: 28px; height: 34px; font-size: 11px; opacity: 0.92; }
.mtile .num { font-size: 16px; font-weight: 800; }
.mtile .kind { font-size: 12px; }
.mtile.hand-tile .num { font-size: 20px; }
.mtile.hand-tile .kind { font-size: 13px; }
.mtile.wan { color: #1d4ed8; }
.mtile.tiao { color: #15803d; }
.mtile.tong { color: #c2410c; }
.mtile.zi { color: #111827; }
.mtile.cai {
  box-shadow: 0 0 0 2px #f59e0b, 0 2px 0 #c4bba6, 0 6px 12px rgba(0, 0, 0, 0.28);
  background: linear-gradient(180deg, #fff7d6 0%, #fde68a 100%);
}
.mtile.an {
  background: linear-gradient(180deg, #334155, #1e293b);
  color: #e2e8f0;
  box-shadow: 0 2px 0 #0f172a, 0 6px 12px rgba(0, 0, 0, 0.35);
}
.actions { display: flex; flex-wrap: wrap; gap: 8px; justify-content: center; align-items: center; }
.actions button, .ghost {
  border: 0;
  border-radius: 8px;
  padding: 8px 14px;
  background: rgba(255, 255, 255, 0.12);
  color: #e8f2ec;
  cursor: pointer;
}
.actions button:disabled { opacity: 0.4; cursor: not-allowed; }
.actions .primary {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-weight: 700;
}
.actions .warn { background: #c2410c; color: #fff; font-weight: 700; }
.ghost {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.22);
}
.hint { color: #fbbf24; font-size: 13px; }
.primary-dark, .ghost-dark {
  border: 0;
  border-radius: 8px;
  padding: 8px 14px;
  cursor: pointer;
}
.primary-dark { background: #0f766e; color: #fff; }
.ghost-dark { background: #e2e8f0; color: #334155; }
.log-box {
  max-width: 1180px;
  margin: 16px auto 0;
  background: rgba(0, 0, 0, 0.22);
  border-radius: 10px;
  padding: 8px 12px;
  color: #b7cfc2;
  font-size: 12px;
}
.log-box summary { cursor: pointer; }
.log-box pre { margin: 8px 0 0; max-height: 160px; overflow: auto; white-space: pre-wrap; }
.modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 40;
}
.modal {
  background: #fff;
  color: #1a1a1a;
  border-radius: 14px;
  padding: 20px;
  width: min(420px, 92vw);
}
.modal ul { padding-left: 18px; }
.win { color: #047857; font-weight: 700; }
.lose { color: #b91c1c; font-weight: 700; }
@media (max-width: 720px) {
  .mid { grid-template-columns: 1fr; }
}
</style>