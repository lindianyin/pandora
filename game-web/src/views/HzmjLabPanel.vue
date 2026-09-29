<script setup lang="ts">
import { computed, onBeforeUnmount } from 'vue'
import { createLabClient } from '../composables/createLabClient'
import type { HzmjMeld } from '../net/hzmjFront'

const props = defineProps<{ slotIndex: number; deviceId: string }>()
const c = createLabClient(props.slotIndex, props.deviceId)

/** Table position: seat0 下 → seat1 右 → seat2 上 → seat3 左（逆时针） */
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
  get wsOk() {
    return c.wsOk.value
  },
  get roomId() {
    return c.roomId.value
  },
})

function isCaishen(t: number) {
  return c.hzmjCaishen.value.includes(t)
}

function seatMelds(seatId: number): HzmjMeld[] {
  return c.hzmjMelds.value[seatId] || []
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
      <span class="meta">{{ c.roomPhase.value || '-' }} · 牌墙 {{ c.hzmjWall.value }} · N={{ c.hzmjN.value }}</span>
    </div>
    <div v-if="c.errorBanner.value" class="err">{{ c.errorBanner.value }}</div>
    <div class="row">
      <button v-if="!c.wsOk.value" :disabled="c.busy.value" @click="c.login()">登录</button>
      <button v-else-if="!c.roomId.value" :disabled="c.matching.value" @click="c.matchHzmj()">匹配</button>
      <button v-if="c.canReady.value" :disabled="c.iAmReady.value" @click="c.doReady()">
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
      <b>{{ c.tileLabel(c.hzmjLastDiscard.value.tile) }}</b>
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
            <i v-for="(t, ti) in m.tiles" :key="ti">{{ c.tileLabel(t) }}</i>
          </span>
        </div>
        <div class="river">
          <i v-for="(t, ti) in c.hzmjRivers.value[s.seat_id] || []" :key="ti">{{ c.tileLabel(t) }}</i>
        </div>
        <div class="lp">{{ c.lastPlays.value[s.seat_id] || '' }}</div>
      </div>
    </div>

    <div class="hand-label">手牌</div>
    <div class="hand">
      <button
        v-for="(t, idx) in c.hand.value"
        :key="idx + '-' + t"
        class="tile"
        :class="{ on: c.selectedHandIndex.value === idx, cai: isCaishen(t) }"
        @click="c.selectTile(idx)"
      >
        {{ c.tileLabel(t) }}
      </button>
    </div>

    <div class="row">
      <template v-if="c.isHzmjDiscardTurn.value">
        <button :disabled="c.selectedHandIndex.value == null" @click="c.doDiscard()">出牌</button>
        <button
          v-for="g in c.anGangCandidates.value"
          :key="'g' + g"
          class="warn"
          @click="c.doAnGang(g)"
        >
          暗杠 {{ c.tileLabel(g) }}
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
      M={{ c.hzmjSettle.value.M }} N={{ c.hzmjSettle.value.N }}
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
  background: #fff;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  padding: 10px;
  font-size: 12px;
}
.panel.active { box-shadow: 0 0 0 2px #f59e0b; }
.panel.claim { box-shadow: 0 0 0 2px #0f766e; }
.ph { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 6px; }
.pos {
  background: #0f766e;
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
