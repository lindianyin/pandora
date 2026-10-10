<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { createPhzLabClient } from '../composables/createPhzLabClient'
import { meldKindLabel, isDarkMeld } from '../net/phzFront'
import { phzIsRed } from '../net/frame'

const router = useRouter()
const c = createPhzLabClient(0, 'phz-table')

onMounted(() => {
  const token = sessionStorage.getItem('pandora_token') || ''
  if (!token) {
    router.replace('/login')
    return
  }
  c.connectWithToken(token)
})
</script>

<template>
  <div class="felt">
    <header class="topbar">
      <div>
        <h1>跑胡子</h1>
        <p>
          {{ c.nickname.value || '玩家' }} · 金币 {{ c.gold.value }}
          <template v-if="c.phzRoundId.value">
            · 局 {{ c.phzRoundId.value }} · 庄 seat{{ c.phzBanker.value }} · 墩 {{ c.phzWall.value }}
            · {{ c.phzSub.value }} · {{ c.countdown.value }}s
          </template>
        </p>
      </div>
      <div class="top-actions">
        <span v-if="c.countdown.value > 0 && c.phzSub.value" class="timer" :class="{ mine: c.isDiscardTurn.value || c.isClaimTurn.value }">
          {{ c.turnBanner.value }} · {{ c.phzSub.value }} · {{ c.countdown.value }}s
        </span>
        <button class="ghost" @click="router.push('/lobby')">大厅</button>
        <button class="ghost" @click="router.push('/phz-lab')">三联 Lab</button>
      </div>
    </header>

    <p v-if="c.errorBanner.value" class="banner err">{{ c.errorBanner.value }}</p>

    <div v-if="c.phzReveal.value" class="reveal">
      <span class="muted">示众 seat{{ c.phzReveal.value.seat }}</span>
      <span class="ptile" :class="{ red: phzIsRed(c.phzReveal.value.tile) }">
        {{ c.phzTileLabel(c.phzReveal.value.tile) }}
      </span>
    </div>

    <div class="melds">
      <div
        v-for="s in 3"
        :key="'m' + s"
        class="seat"
        :class="{ me: s - 1 === c.mySeat.value, turn: s - 1 === c.turnSeat.value }"
      >
        <div class="seat-title">seat{{ s - 1 }} 门前</div>
        <div class="meld-row">
          <span
            v-for="(m, mi) in c.phzMelds.value[s - 1] || []"
            :key="mi"
            class="meld-group"
            :class="{ dark: isDarkMeld(m.kind) }"
          >
            <em>{{ meldKindLabel(m.kind) }}</em>
            <span
              v-for="(t, ti) in m.tiles"
              :key="ti"
              class="ptile sm"
              :class="{ red: phzIsRed(t) }"
            >{{ c.phzTileLabel(t) }}</span>
          </span>
          <span v-if="!(c.phzMelds.value[s - 1] || []).length" class="muted">暂无</span>
        </div>
      </div>
    </div>

    <div class="hand-wrap">
      <div class="hand-label">手牌 · 点选后出牌</div>
      <div class="hand">
        <button
          v-for="(t, i) in c.hand.value"
          :key="i + '-' + t"
          type="button"
          class="ptile hand-tile"
          :class="{ on: c.selectedHandIndex.value === i, red: phzIsRed(t) }"
          @click="c.selectTile(i)"
        >
          {{ c.phzTileLabel(t) }}
        </button>
      </div>
    </div>

    <div class="actions" v-if="c.isDiscardTurn.value || c.isClaimTurn.value || c.canReady.value">
      <button v-if="c.canReady.value && !c.iAmReady.value" class="primary" @click="c.doReady()">准备</button>
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

    <div v-if="c.showSettle.value" class="modal-mask">
      <div class="modal">
        <template v-if="c.settle.value">
          <h3>结算</h3>
          <p>
            息{{ c.settle.value.hu_xi }} 囤{{ c.settle.value.tun }} 番{{ c.settle.value.fan }} ·
            {{ c.settleMingTang.value }}
          </p>
          <p v-for="e in c.settle.value.entries" :key="e.seat_id">
            seat{{ e.seat_id }}
            <span :class="e.delta_gold >= 0 ? 'win' : 'lose'">{{ e.delta_gold }}</span>
          </p>
        </template>
        <template v-else>
          <h3>流局</h3>
        </template>
        <button class="primary-dark" @click="c.dismissSettle()">关闭</button>
      </div>
    </div>

    <details class="log-box">
      <summary>日志</summary>
      <ul>
        <li v-for="(line, i) in c.logs.value" :key="i">{{ line }}</li>
      </ul>
    </details>
  </div>
</template>

<style scoped>
.felt {
  min-height: 100vh;
  padding: 16px 18px 28px;
  background:
    radial-gradient(ellipse at top, rgba(55, 85, 55, 0.5), transparent 55%),
    linear-gradient(180deg, #1a2e1c 0%, #0d1610 100%);
  color: #e8f2ec;
  max-width: 960px;
  margin: 0 auto;
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
.timer.mine { background: rgba(232, 197, 71, 0.15); }
.banner.err {
  background: rgba(185, 28, 28, 0.25);
  color: #ffb4b4;
  padding: 8px 12px;
  border-radius: 8px;
}
.reveal {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  background: rgba(0, 0, 0, 0.22);
  border-radius: 12px;
  padding: 10px 14px;
  margin-bottom: 12px;
}
.melds { display: flex; flex-direction: column; gap: 8px; margin-bottom: 14px; }
.seat {
  background: rgba(0, 0, 0, 0.22);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 12px;
  padding: 10px 12px;
}
.seat.me { border-color: rgba(232, 197, 71, 0.4); background: rgba(232, 197, 71, 0.08); }
.seat.turn { box-shadow: 0 0 0 2px #e8c547; }
.seat-title { font-size: 12px; color: #9db5a8; margin-bottom: 6px; }
.meld-row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.meld-group {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  padding: 3px 6px;
  background: rgba(255, 255, 255, 0.06);
  border-radius: 8px;
}
.meld-group.dark { background: rgba(0, 0, 0, 0.28); }
.meld-group em { font-style: normal; font-size: 11px; color: #9db5a8; margin-right: 2px; }
.hand-wrap {
  background: rgba(0, 0, 0, 0.18);
  border-radius: 14px;
  padding: 12px;
  margin-bottom: 12px;
}
.hand-label { font-size: 12px; color: #9db5a8; margin-bottom: 8px; }
.hand { display: flex; flex-wrap: wrap; gap: 8px; min-height: 72px; }
.ptile {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 40px;
  height: 52px;
  padding: 0 8px;
  border: 0;
  border-radius: 8px;
  background: linear-gradient(180deg, #fffef8 0%, #efe9da 100%);
  color: #1a1a1a;
  font-weight: 800;
  box-shadow: 0 2px 0 #c4bba6, 0 6px 12px rgba(0, 0, 0, 0.28);
}
.ptile.sm { min-width: 28px; height: 34px; font-size: 12px; }
.ptile.hand-tile {
  width: 48px;
  height: 68px;
  cursor: pointer;
  font-size: 15px;
  transition: transform 0.12s;
}
.ptile.hand-tile:hover { transform: translateY(-4px); }
.ptile.hand-tile.on {
  outline: 2px solid #e8c547;
  outline-offset: 2px;
  transform: translateY(-10px);
}
.ptile.red { color: #c62828; }
.actions { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 12px; }
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
.muted { color: #6f877a; font-size: 13px; }
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
  width: min(400px, 92vw);
}
.primary-dark {
  border: 0;
  background: #0f766e;
  color: #fff;
  border-radius: 8px;
  padding: 8px 14px;
  cursor: pointer;
}
.win { color: #047857; font-weight: 700; }
.lose { color: #b91c1c; font-weight: 700; }
.log-box {
  background: rgba(0, 0, 0, 0.22);
  border-radius: 10px;
  padding: 8px 12px;
  color: #b7cfc2;
  font-size: 12px;
}
.log-box summary { cursor: pointer; }
.log-box ul {
  list-style: none;
  margin: 8px 0 0;
  padding: 0;
  max-height: 160px;
  overflow: auto;
}
</style>