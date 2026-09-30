<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useGameSession } from '../composables/useGameSession'
import { meldKindLabel, type HzmjMeld } from '../net/hzmjFront'

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

function faceText(m: HzmjMeld, t: number) {
  return m.kind === 4 ? '?' : tileLabel(t)
}
</script>

<template>
  <section class="card profile">
    <div>UID: {{ uid }} · {{ nickname }} · 金币 {{ gold }} / 钻石 {{ diamond }}</div>
    <div>
      WSS: {{ wsOk ? '已连接' : '未连接' }} · 房间 #{{ roomId }} · {{ roomPhase || hzmjSub || '-' }}
      · 余牌 {{ hzmjWall }} · N={{ hzmjN }} · 局 {{ hzmjRoundId || '-' }} · 连庄 {{ hzmjLian }}
    </div>
  </section>

  <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>
  <div v-if="reconnectHint" class="banner ok">{{ reconnectHint }}</div>

  <section class="card table">
    <div class="table-head">
      <h2>杭州麻将</h2>
      <span class="meta">
        财神
        <span v-for="tile in hzmjCaishen" :key="tile" class="cai">{{ tileLabel(tile) }}</span>
        · 庄 {{ hzmjBanker }}
        <span v-if="hzmjPiaoSeat >= 0" class="piao">财飘 seat{{ hzmjPiaoSeat }}</span>
      </span>
      <span v-if="countdown > 0 && hzmjSub" class="cd" :class="{ mine: isMyTurn || isHzmjClaim }">
        {{ isHzmjClaim ? '请选择' : isMyTurn ? '请出牌' : `等待${turnSeat}` }}
        · {{ hzmjSub }} · {{ countdown }}s
      </span>
    </div>

    <div class="arena">
      <div class="opp">
        <div v-if="seatOpposite" class="seat" :class="{ turn: seatOpposite.seat_id === turnSeat }">
          <div class="name">
            {{ seatOpposite.nickname }}
            <span v-if="seatOpposite.seat_id === hzmjBanker" class="tag">庄</span>
          </div>
          <div class="info">
            {{ seatCards(seatOpposite.seat_id) }}张
            <span v-if="seatOpposite.trusteeship" class="trust">托管</span>
          </div>
          <div class="front">
            <div v-if="seatMelds(seatOpposite.seat_id).length" class="meld-row">
              <div v-for="(m, mi) in seatMelds(seatOpposite.seat_id)" :key="'om' + mi" class="meld-group">
                <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                <span
                  v-for="(tile, ti) in m.tiles"
                  :key="'omt' + ti"
                  class="face"
                  :class="{ cai: isCaishen(tile), an: m.kind === 4 }"
                >
                  {{ faceText(m, tile) }}
                </span>
              </div>
            </div>
            <div v-if="seatRiver(seatOpposite.seat_id).length" class="river">
              <span
                v-for="(tile, ti) in seatRiver(seatOpposite.seat_id)"
                :key="'or' + ti"
                class="face sm"
                :class="{ cai: isCaishen(tile) }"
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
            </div>
            <div class="info">{{ seatCards(seatLeft.seat_id) }}张</div>
            <div class="front">
              <div v-if="seatMelds(seatLeft.seat_id).length" class="meld-row">
                <div v-for="(m, mi) in seatMelds(seatLeft.seat_id)" :key="'lm' + mi" class="meld-group">
                  <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                  <span
                    v-for="(tile, ti) in m.tiles"
                    :key="'lmt' + ti"
                    class="face"
                    :class="{ cai: isCaishen(tile), an: m.kind === 4 }"
                  >
                    {{ faceText(m, tile) }}
                  </span>
                </div>
              </div>
              <div v-if="seatRiver(seatLeft.seat_id).length" class="river">
                <span
                  v-for="(tile, ti) in seatRiver(seatLeft.seat_id)"
                  :key="'lr' + ti"
                  class="face sm"
                  :class="{ cai: isCaishen(tile) }"
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
            打出 seat{{ hzmjLastDiscard.seat }}
            <span class="tile" :class="{ cai: isCaishen(hzmjLastDiscard.tile) }">
              {{ tileLabel(hzmjLastDiscard.tile) }}
            </span>
          </div>
          <div v-else class="discard muted">等待出牌</div>
        </div>

        <div class="side right">
          <div v-if="seatRight" class="seat" :class="{ turn: seatRight.seat_id === turnSeat }">
            <div class="name">
              {{ seatRight.nickname }}
              <span v-if="seatRight.seat_id === hzmjBanker" class="tag">庄</span>
            </div>
            <div class="info">{{ seatCards(seatRight.seat_id) }}张</div>
            <div class="front">
              <div v-if="seatMelds(seatRight.seat_id).length" class="meld-row">
                <div v-for="(m, mi) in seatMelds(seatRight.seat_id)" :key="'rm' + mi" class="meld-group">
                  <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                  <span
                    v-for="(tile, ti) in m.tiles"
                    :key="'rmt' + ti"
                    class="face"
                    :class="{ cai: isCaishen(tile), an: m.kind === 4 }"
                  >
                    {{ faceText(m, tile) }}
                  </span>
                </div>
              </div>
              <div v-if="seatRiver(seatRight.seat_id).length" class="river">
                <span
                  v-for="(tile, ti) in seatRiver(seatRight.seat_id)"
                  :key="'rr' + ti"
                  class="face sm"
                  :class="{ cai: isCaishen(tile) }"
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
          </div>
          <div class="info">
            {{ seatSelf.ready ? '已准备' : '未准备' }} · {{ seatCards(seatSelf.seat_id) }}张
            <span v-if="seatSelf.trusteeship" class="trust">托管</span>
          </div>
          <div class="front">
            <div v-if="seatMelds(seatSelf.seat_id).length" class="meld-row">
              <div v-for="(m, mi) in seatMelds(seatSelf.seat_id)" :key="'sm' + mi" class="meld-group">
                <span class="meld-tag">{{ meldKindLabel(m.kind) }}</span>
                <span
                  v-for="(tile, ti) in m.tiles"
                  :key="'smt' + ti"
                  class="face"
                  :class="{ cai: isCaishen(tile), an: m.kind === 4 }"
                >
                  {{ faceText(m, tile) }}
                </span>
              </div>
            </div>
            <div v-if="seatRiver(seatSelf.seat_id).length" class="river">
              <span
                v-for="(tile, ti) in seatRiver(seatSelf.seat_id)"
                :key="'sr' + ti"
                class="face sm"
                :class="{ cai: isCaishen(tile) }"
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
            class="tile-btn"
            :class="{ on: selectedHandIndex === idx, cai: isCaishen(tile) }"
            @click="selectTile(idx)"
          >
            {{ tileLabel(tile) }}
          </button>
        </div>

        <div class="row actions">
          <button v-if="canReady" type="button" :disabled="iAmReady" @click="doReady">
            {{ iAmReady ? '已准备' : '准备' }}
          </button>
          <template v-if="isHzmjDiscardTurn">
            <button v-if="canZimoHu" type="button" class="warn" @click="doHzmjAction(4)">自摸</button>
            <button type="button" :disabled="selectedHandIndex == null" @click="doHzmjDiscard">出牌</button>
            <button
              v-for="g in anGangCandidates"
              :key="'gang-' + g"
              type="button"
              class="warn"
              @click="doHzmjAnGang(g)"
            >
              暗杠 {{ tileLabel(g) }}
            </button>
            <button
              v-for="g in buGangCandidates"
              :key="'bugang-' + g"
              type="button"
              class="warn"
              @click="doHzmjBuGang(g)"
            >
              补杠 {{ tileLabel(g) }}
            </button>
          </template>
          <template v-if="isHzmjClaim">
            <span v-if="hzmjClaimHint" class="hint">{{ hzmjClaimHint }}</span>
            <button type="button" :disabled="hzmjClaimSent" @click="doHzmjAction(0)">过</button>
            <button v-if="canClaimChi" type="button" :disabled="hzmjClaimSent" @click="doHzmjAction(1)">吃</button>
            <button v-if="canClaimPeng" type="button" :disabled="hzmjClaimSent" @click="doHzmjAction(2)">碰</button>
            <button v-if="canClaimGang" type="button" :disabled="hzmjClaimSent" @click="doHzmjAction(3)">杠</button>
            <button
              v-if="canClaimHu"
              type="button"
              class="warn"
              :disabled="hzmjClaimSent"
              @click="doHzmjAction(4)"
            >
              胡
            </button>
          </template>
          <button type="button" class="leave" @click="backLobby">返回大厅</button>
        </div>
      </div>
    </div>
  </section>

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
      <div class="row">
        <button @click="doReady">再来一局</button>
        <button class="ghost" @click="backLobby">返回大厅</button>
        <button class="ghost" @click="closeSettleStay">关闭</button>
      </div>
    </div>
  </div>

  <div v-if="showLiuJu" class="modal-mask">
    <div class="modal">
      <h3>流局</h3>
      <p>连庄 {{ hzmjLian }}</p>
      <div class="row">
        <button @click="doReady">再来一局</button>
        <button class="ghost" @click="backLobby">返回大厅</button>
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
.table-head {
  display: flex;
  flex-wrap: wrap;
  justify-content: space-between;
  align-items: center;
  gap: 10px;
}
.table-head h2 { margin: 0; }
.meta { color: #64748b; font-size: 13px; }
.cai {
  display: inline-block;
  margin-left: 4px;
  background: #fef3c7;
  color: #b45309;
  padding: 1px 6px;
  border-radius: 4px;
  font-weight: 600;
}
.piao { margin-left: 8px; color: #c026d3; font-weight: 600; }
.cd {
  background: #f1f5f9;
  color: #475569;
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 13px;
}
.cd.mine { background: #fef3c7; color: #b45309; }
.arena { margin-top: 12px; display: flex; flex-direction: column; gap: 12px; }
.opp { display: flex; justify-content: center; }
.mid {
  display: grid;
  grid-template-columns: 1fr 1.2fr 1fr;
  gap: 12px;
  align-items: center;
}
.center { text-align: center; }
.discard { font-size: 16px; font-weight: 600; }
.discard.muted { color: #94a3b8; font-weight: 400; }
.discard .tile {
  display: inline-block;
  margin-left: 8px;
  padding: 6px 10px;
  border: 1px solid #cbd5e1;
  border-radius: 6px;
  background: #f8fafc;
}
.discard .tile.cai { background: #fef3c7; border-color: #f59e0b; }
.seat {
  border: 1px solid #ddd;
  border-radius: 8px;
  padding: 10px;
  font-size: 13px;
  position: relative;
  min-height: 72px;
  background: #fafafa;
}
.seat.me { border-color: #0f766e; background: #f0fdfa; }
.seat.turn { box-shadow: 0 0 0 2px #f59e0b; }
.tag {
  margin-left: 6px;
  background: #b45309;
  color: #fff;
  font-size: 11px;
  padding: 2px 6px;
  border-radius: 4px;
}
.trust { color: #b91c1c; margin-left: 6px; }
.front { margin-top: 6px; display: flex; flex-direction: column; gap: 4px; }
.meld-row { display: flex; flex-wrap: wrap; gap: 8px; }
.meld-group {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  padding: 2px 4px;
  background: #e2e8f0;
  border-radius: 6px;
}
.meld-tag {
  font-size: 10px;
  color: #475569;
  margin-right: 2px;
  font-weight: 600;
}
.face {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 28px;
  height: 32px;
  padding: 0 4px;
  border: 1px solid #94a3b8;
  border-radius: 4px;
  background: #fff;
  font-size: 12px;
  font-weight: 600;
  color: #0f172a;
}
.face.sm {
  min-width: 24px;
  height: 26px;
  font-size: 11px;
  opacity: 0.9;
}
.face.cai { background: #fef3c7; border-color: #f59e0b; color: #b45309; }
.face.an { background: #334155; color: #e2e8f0; border-color: #1e293b; }
.river {
  display: flex;
  flex-wrap: wrap;
  gap: 3px;
  max-width: 100%;
}
.play { margin-top: 6px; color: #0f172a; font-weight: 600; min-height: 1.2em; }
.hand { display: flex; flex-wrap: wrap; gap: 6px; margin: 12px 0; min-height: 40px; }
.tile-btn {
  background: #f8fafc;
  color: #111;
  border: 1px solid #cbd5e1;
  min-width: 48px;
  padding: 8px 10px;
  border-radius: 6px;
  cursor: pointer;
}
.tile-btn.on { background: #0f766e; color: #fff; border-color: #0f766e; }
.tile-btn.cai { border-color: #f59e0b; }
.tile-btn.cai.on { background: #b45309; border-color: #b45309; }
.row { display: flex; gap: 8px; flex-wrap: wrap; }
button {
  border: 0;
  background: #0f766e;
  color: #fff;
  padding: 8px 14px;
  border-radius: 6px;
  cursor: pointer;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.ghost { background: #e2e8f0; color: #334155; }
button.leave {
  background: #fff;
  color: #334155;
  border: 1px solid #94a3b8;
}
button.warn { background: #c2410c; }
.hint {
  align-self: center;
  color: #b45309;
  font-size: 13px;
  margin-right: 4px;
}
.banner { padding: 8px 12px; border-radius: 6px; margin-bottom: 12px; }
.banner.err { background: #fef2f2; color: #b91c1c; }
.banner.ok { background: #ecfdf5; color: #047857; }
.modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(15, 23, 42, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 40;
}
.modal {
  background: #fff;
  border-radius: 12px;
  padding: 20px;
  width: min(420px, 92vw);
}
.modal ul { padding-left: 18px; }
.win { color: #047857; font-weight: 700; }
.lose { color: #b91c1c; font-weight: 700; }
.log {
  margin: 0;
  max-height: 240px;
  overflow: auto;
  background: #0f172a;
  color: #e2e8f0;
  padding: 12px;
  border-radius: 8px;
  font-size: 12px;
}
</style>
