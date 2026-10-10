<script setup lang="ts">
import { computed, onBeforeUnmount } from 'vue'
import { createLabClient } from '../composables/createLabClient'
import type { HzmjMeld } from '../net/hzmjFront'
import { hzmjTileFace } from '../net/frame'

const props = defineProps<{ slotIndex: number; deviceId: string }>()
const c = createLabClient(props.slotIndex, props.deviceId)

const tablePos = computed(() => {
  const seat = c.mySeat.value >= 0 ? c.mySeat.value : props.slotIndex
  return Math.min(3, Math.max(0, seat))
})
const posLabel = computed(() => ['下', '右', '上', '左'][tablePos.value] || '')

onBeforeUnmount(() => c.dispose())

defineExpose({
  login: () => c.login(),
  matchHzmj: () => c.matchHzmj(),
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

function isCaishen(t: number) {
  return c.hzmjCaishen.value.includes(t)
}

function seatMelds(seatId: number): HzmjMeld[] {
  return c.hzmjMelds.value[seatId] || []
}

function face(t: number) {
  return hzmjTileFace(t)
}
</script>

<template>
  <section
    class="panel"
    :class="[
      `pos-s${tablePos}`,
      { active: c.isHzmjDiscardTurn.value || c.isHzmjClaim.value, claim: c.isHzmjClaim.value },
    ]"
  >
    <div class="ph">
      <strong>{{ c.label }}</strong>
      <span class="pos">{{ posLabel }}</span>
      <span class="dev" :title="c.deviceId">{{ c.deviceId }}</span>
      <span v-if="c.uid.value">uid {{ c.uid.value }} {{ c.nickname.value }}</span>
      <span v-else>offline</span>
      <span v-if="c.mySeat.value >= 0">座{{ c.mySeat.value }}</span>
      <span v-if="c.mySeat.value === c.hzmjBanker.value" class="tag">庄</span>
      <span v-if="c.hzmjRoundId.value" class="tag">局 {{ c.hzmjRoundId.value }}</span>
      <span class="meta">{{ c.roomPhase.value || '-' }} · 牌墙 {{ c.hzmjWall.value }} · N={{ c.hzmjN.value }}</span>
    </div>
    <div v-if="c.errorBanner.value" class="err">{{ c.errorBanner.value }}</div>
    <div class="row">
      <button v-if="!c.wsOk.value" :disabled="c.busy.value" @click="c.login()">登录</button>
      <button v-else-if="!c.roomId.value" :disabled="c.matching.value" @click="c.matchHzmj()">匹配</button>
      <button v-if="c.canReady.value" class="primary" :disabled="c.iAmReady.value" @click="c.doReady()">
        {{ c.iAmReady.value ? 'OK' : '准备' }}
      </button>
      <button v-if="c.roomId.value" class="ghost" @click="c.leave()">离开</button>
    </div>

    <div v-if="c.countdown.value > 0 && c.hzmjSub.value" class="cd">
      {{ c.isHzmjClaim.value ? '鸣牌' : c.isMyTurn.value ? '你的回合' : '座' + c.turnSeat.value }}
      · {{ c.hzmjSub.value }} · {{ c.countdown.value }}s
    </div>

    <div v-if="c.hzmjLastDiscard.value" class="disc">
      出牌 seat{{ c.hzmjLastDiscard.value.seat }}
      <span class="mtile sm" :class="[face(c.hzmjLastDiscard.value.tile).suit, { cai: isCaishen(c.hzmjLastDiscard.value.tile) }]">
        {{ c.tileLabel(c.hzmjLastDiscard.value.tile) }}
      </span>
    </div>

    <div class="seats">
      <div
        v-for="s in c.seats.value"
        :key="s.seat_id"
        class="mini"
        :class="{ me: s.seat_id === c.mySeat.value, turn: s.seat_id === c.turnSeat.value }"
      >
        <div>{{ s.nickname }} 座{{ s.seat_id }} 剩{{ c.cardsLeft.value[s.seat_id] ?? '-' }}</div>
        <div class="melds">
          <span v-for="(m, mi) in seatMelds(s.seat_id)" :key="mi" class="mg">
            {{ c.meldKindLabel(m.kind) }}
            <i
              v-for="(t, ti) in m.tiles"
              :key="ti"
              class="mtile xs"
              :class="[m.kind === 4 ? 'an' : face(t).suit, { cai: isCaishen(t) }]"
            >{{ m.kind === 4 ? '?' : c.tileLabel(t) }}</i>
          </span>
        </div>
        <div class="river">
          <i
            v-for="(t, ti) in c.hzmjRivers.value[s.seat_id] || []"
            :key="ti"
            class="mtile xs"
            :class="[face(t).suit, { cai: isCaishen(t) }]"
          >{{ c.tileLabel(t) }}</i>
        </div>
        <div class="lp">{{ c.lastPlays.value[s.seat_id] || '' }}</div>
      </div>
    </div>

    <div class="hand-label">手牌</div>
    <div class="hand">
      <button
        v-for="(t, idx) in c.hand.value"
        :key="idx + '-' + t"
        type="button"
        class="mtile"
        :class="[face(t).suit, { on: c.selectedHandIndex.value === idx, cai: isCaishen(t) }]"
        @click="c.selectTile(idx)"
      >
        <span class="num">{{ face(t).num }}</span>
        <span class="kind">{{ face(t).kind }}</span>
      </button>
    </div>

    <div class="row">
      <template v-if="c.isHzmjDiscardTurn.value">
        <button v-if="c.canZimoHu.value" class="warn" @click="c.doAction(4)">自摸</button>
        <button class="primary" :disabled="c.selectedHandIndex.value == null" @click="c.doDiscard()">出牌</button>
        <button v-for="g in c.anGangCandidates.value" :key="'g' + g" class="warn" @click="c.doAnGang(g)">
          暗杠 {{ c.tileLabel(g) }}
        </button>
        <button v-for="g in c.buGangCandidates.value" :key="'bg' + g" class="warn" @click="c.doBuGang(g)">
          补杠 {{ c.tileLabel(g) }}
        </button>
      </template>
      <template v-if="c.isHzmjClaim.value">
        <span v-if="c.hzmjClaimHint.value" class="hint">{{ c.hzmjClaimHint.value }}</span>
        <button :disabled="c.hzmjClaimSent.value" @click="c.doAction(0)">过</button>
        <button v-if="c.canClaimChi.value" :disabled="c.hzmjClaimSent.value" @click="c.doAction(1)">吃</button>
        <button v-if="c.canClaimPeng.value" :disabled="c.hzmjClaimSent.value" @click="c.doAction(2)">碰</button>
        <button v-if="c.canClaimGang.value" :disabled="c.hzmjClaimSent.value" @click="c.doAction(3)">杠</button>
        <button v-if="c.canClaimHu.value" class="warn" :disabled="c.hzmjClaimSent.value" @click="c.doAction(4)">胡</button>
      </template>
    </div>

    <div v-if="c.showSettle.value && c.hzmjSettle.value" class="settle">
      <strong>结算</strong>
      {{ c.hzmjSettle.value.is_zimo ? '自摸' : '点炮' }}
      <span v-if="!c.hzmjSettle.value.is_zimo && c.hzmjSettle.value.shooter_seat >= 0">
        (seat{{ c.hzmjSettle.value.shooter_seat }})
      </span>
      M={{ c.hzmjSettle.value.M }} N={{ c.hzmjSettle.value.N }}
      <span v-if="c.hzmjSettle.value.hu_tile >= 0">胡 {{ c.tileLabel(c.hzmjSettle.value.hu_tile) }}</span>
      <span v-for="e in c.hzmjSettle.value.entries" :key="e.uid">
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
.panel.claim { box-shadow: 0 0 0 2px #34d399, 0 10px 24px rgba(0, 0, 0, 0.28); }
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
.err { background: rgba(185, 28, 28, 0.25); color: #ffb4b4; padding: 4px 8px; border-radius: 6px; margin-bottom: 6px; }
.row { display: flex; flex-wrap: wrap; gap: 6px; margin: 6px 0; align-items: center; }
.cd {
  background: rgba(232, 197, 71, 0.15);
  color: #e8c547;
  border: 1px solid rgba(232, 197, 71, 0.35);
  padding: 4px 10px;
  border-radius: 999px;
  display: inline-block;
  margin-bottom: 6px;
}
.disc { font-weight: 600; margin-bottom: 6px; display: flex; align-items: center; gap: 6px; }
.seats { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; margin-bottom: 6px; }
.mini {
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 8px;
  padding: 6px;
  background: rgba(0, 0, 0, 0.18);
}
.mini.me { border-color: rgba(232, 197, 71, 0.45); }
.mini.turn { box-shadow: 0 0 0 1px #e8c547; }
.melds, .river { display: flex; flex-wrap: wrap; gap: 3px; margin-top: 3px; }
.mg {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  background: rgba(255, 255, 255, 0.06);
  border-radius: 4px;
  padding: 1px 3px;
  margin-right: 2px;
}
.lp { min-height: 1em; font-weight: 700; }
.hand-label { color: #9db5a8; margin-top: 4px; }
.hand { display: flex; flex-wrap: wrap; gap: 5px; min-height: 52px; }
.mtile {
  display: inline-flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  width: 40px;
  height: 56px;
  border: 0;
  border-radius: 6px;
  background: linear-gradient(180deg, #fffef8 0%, #efe9da 100%);
  color: #1a1a1a;
  box-shadow: 0 2px 0 #c4bba6, 0 4px 10px rgba(0, 0, 0, 0.25);
  cursor: pointer;
  padding: 0;
  font-style: normal;
  font-weight: 700;
}
.mtile.sm, .mtile.xs {
  width: auto;
  min-width: 26px;
  height: 30px;
  padding: 0 4px;
  font-size: 11px;
  cursor: default;
}
.mtile .num { font-size: 14px; font-weight: 800; line-height: 1; }
.mtile .kind { font-size: 11px; line-height: 1; }
.mtile.wan { color: #1d4ed8; }
.mtile.tiao { color: #15803d; }
.mtile.tong { color: #c2410c; }
.mtile.zi { color: #111827; }
.mtile.cai { background: linear-gradient(180deg, #fff7d6, #fde68a); box-shadow: 0 0 0 2px #f59e0b, 0 2px 0 #c4bba6; }
.mtile.an { background: #1e293b; color: #e2e8f0; }
.mtile.on { outline: 2px solid #e8c547; outline-offset: 1px; transform: translateY(-6px); }
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
.hint { color: #fbbf24; }
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