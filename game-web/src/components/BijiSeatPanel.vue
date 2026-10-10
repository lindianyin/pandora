<script setup lang="ts">
import type { createBijiLabClient } from '../composables/createBijiLabClient'
import type { BijiCardFace } from '../net/frame'

type Client = ReturnType<typeof createBijiLabClient>

const props = defineProps<{
  client: Client
  deviceId: string
  compact?: boolean
}>()

function onPoolClick(card: BijiCardFace) {
  props.client.toggleSelect(card.id)
}

function onDunCard(zone: 'head' | 'mid' | 'tail', card: BijiCardFace) {
  props.client.takeFrom(zone, card.id)
}

function onDunZone(zone: 'head' | 'mid' | 'tail') {
  props.client.placeTo(zone)
}
</script>

<template>
  <section class="seat" :class="{ compact }">
    <header class="seat-head">
      <div>
        <h2>{{ client.label }} · {{ client.nickname.value }}</h2>
        <p class="meta">
          uid {{ client.uid.value }} · 金币 {{ client.gold.value }} · 房间
          {{ client.roomId.value || '-' }} · {{ client.roomPhase.value || 'idle' }}
        </p>
        <p class="hw">{{ deviceId }}</p>
      </div>
      <div v-if="client.arranging.value || client.remainS.value > 0" class="timer">
        <span class="timer-label">摆牌</span>
        <strong>{{ client.remainS.value }}</strong>
        <span>秒</span>
      </div>
    </header>

    <p v-if="client.errorBanner.value" class="err">{{ client.errorBanner.value }}</p>
    <p v-else-if="client.settleText.value" class="settle">结算 {{ client.settleText.value }}</p>
    <p
      v-else-if="client.arranging.value"
      class="hint"
      :class="{ bad: client.complete.value && !client.legal.value, ok: client.canConfirm.value }"
    >
      {{ client.arrangeHint.value }}
    </p>
    <p v-else-if="client.locked.value" class="hint ok">已确认，等待比牌…</p>

    <div class="duns">
      <div
        class="dun"
        :class="{ active: client.selected.value != null && client.head.value.length < 3 }"
        @click="onDunZone('head')"
      >
        <div class="dun-title">
          <span>头墩</span>
          <em>{{ client.headType.value || '3张' }}</em>
        </div>
        <div class="slots">
          <button
            v-for="card in client.headFaces.value"
            :key="'h' + card.id"
            type="button"
            class="card"
            :class="{ red: card.red }"
            :disabled="client.locked.value"
            @click.stop="onDunCard('head', card)"
          >
            <span class="rank">{{ card.rank }}</span>
            <span class="suit">{{ card.suit }}</span>
          </button>
          <div v-for="n in 3 - client.headFaces.value.length" :key="'he' + n" class="slot" />
        </div>
      </div>
      <div
        class="dun"
        :class="{ active: client.selected.value != null && client.mid.value.length < 3 }"
        @click="onDunZone('mid')"
      >
        <div class="dun-title">
          <span>中墩</span>
          <em>{{ client.midType.value || '3张' }}</em>
        </div>
        <div class="slots">
          <button
            v-for="card in client.midFaces.value"
            :key="'m' + card.id"
            type="button"
            class="card"
            :class="{ red: card.red }"
            :disabled="client.locked.value"
            @click.stop="onDunCard('mid', card)"
          >
            <span class="rank">{{ card.rank }}</span>
            <span class="suit">{{ card.suit }}</span>
          </button>
          <div v-for="n in 3 - client.midFaces.value.length" :key="'me' + n" class="slot" />
        </div>
      </div>
      <div
        class="dun tail"
        :class="{ active: client.selected.value != null && client.tail.value.length < 3 }"
        @click="onDunZone('tail')"
      >
        <div class="dun-title">
          <span>尾墩</span>
          <em>{{ client.tailType.value || '3张' }}</em>
        </div>
        <div class="slots">
          <button
            v-for="card in client.tailFaces.value"
            :key="'t' + card.id"
            type="button"
            class="card"
            :class="{ red: card.red }"
            :disabled="client.locked.value"
            @click.stop="onDunCard('tail', card)"
          >
            <span class="rank">{{ card.rank }}</span>
            <span class="suit">{{ card.suit }}</span>
          </button>
          <div v-for="n in 3 - client.tailFaces.value.length" :key="'te' + n" class="slot" />
        </div>
      </div>
    </div>

    <div class="pool-wrap">
      <div class="pool-title">手牌区 · 先点牌再点墩位放入 · 点墩内牌可取回</div>
      <div class="pool">
        <button
          v-for="card in client.poolFaces.value"
          :key="'p' + card.id"
          type="button"
          class="card"
          :class="{ red: card.red, selected: client.selected.value === card.id }"
          :disabled="client.locked.value"
          @click="onPoolClick(card)"
        >
          <span class="rank">{{ card.rank }}</span>
          <span class="suit">{{ card.suit }}</span>
        </button>
        <span v-if="!client.poolFaces.value.length && client.arranging.value" class="empty">手牌已全部入墩</span>
      </div>
    </div>

    <div class="actions">
      <button class="primary" :disabled="!client.canConfirm.value" @click="client.confirmArrange()">
        确认摆牌
      </button>
      <button :disabled="!client.arranging.value" @click="client.smartFill()">智能摆牌</button>
      <button :disabled="!client.arranging.value" @click="client.clearBoard()">清空重摆</button>
      <button :disabled="!client.arranging.value" @click="client.autoConfirm()">智能并确认</button>
      <button :disabled="!client.canMatch.value" @click="client.oneClickMatch()">匹配</button>
      <button :disabled="!client.canReady.value" @click="client.oneClickReady()">准备</button>
      <button @click="client.topupGold()">加币</button>
      <button class="ghost" @click="client.oneClickLeave()">离开</button>
    </div>

    <pre v-if="!compact" class="log">{{ client.logs.value.join('\n') }}</pre>
  </section>
</template>

<style scoped>
.seat {
  --felt: #0f3d2e;
  --felt-edge: #0a2a20;
  --ink: #f3f7f4;
  --muted: #9db5a8;
  --gold: #e8c547;
  --danger: #ff8a80;
  background: linear-gradient(160deg, #14523c 0%, var(--felt) 45%, var(--felt-edge) 100%);
  color: var(--ink);
  border-radius: 16px;
  padding: 14px 14px 12px;
  box-shadow: 0 10px 28px rgba(0, 0, 0, 0.28), inset 0 1px 0 rgba(255, 255, 255, 0.08);
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.seat.compact { padding: 12px; }
.seat-head {
  display: flex;
  justify-content: space-between;
  gap: 10px;
  align-items: flex-start;
}
.seat-head h2 {
  margin: 0;
  font-size: 1.05rem;
  font-weight: 700;
}
.meta, .hw {
  margin: 2px 0 0;
  font-size: 12px;
  color: var(--muted);
}
.hw { font-family: ui-monospace, Consolas, monospace; word-break: break-all; }
.timer {
  display: flex;
  align-items: baseline;
  gap: 4px;
  background: rgba(0, 0, 0, 0.28);
  border: 1px solid rgba(232, 197, 71, 0.45);
  border-radius: 999px;
  padding: 6px 12px;
  color: var(--gold);
  white-space: nowrap;
}
.timer strong { font-size: 1.35rem; font-variant-numeric: tabular-nums; }
.timer-label { font-size: 12px; opacity: 0.85; }
.hint { margin: 0; font-size: 13px; color: #d7e8de; }
.hint.ok { color: #9be7c4; }
.hint.bad { color: var(--danger); }
.err { margin: 0; color: var(--danger); font-size: 13px; }
.settle { margin: 0; color: var(--gold); font-size: 13px; }
.duns {
  display: grid;
  grid-template-columns: 1fr 1fr 1fr;
  gap: 8px;
}
.dun {
  background: rgba(0, 0, 0, 0.18);
  border: 1px dashed rgba(255, 255, 255, 0.18);
  border-radius: 12px;
  padding: 8px;
  cursor: pointer;
  transition: border-color 0.15s, background 0.15s, transform 0.15s;
}
.dun.active {
  border-color: var(--gold);
  background: rgba(232, 197, 71, 0.12);
  transform: translateY(-1px);
}
.dun.tail { border-style: solid; border-color: rgba(232, 197, 71, 0.28); }
.dun-title {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
  font-size: 12px;
  color: var(--muted);
}
.dun-title em { font-style: normal; color: #cfe7da; }
.slots {
  display: flex;
  gap: 6px;
  min-height: 72px;
  align-items: center;
}
.slot {
  width: 48px;
  height: 68px;
  border-radius: 8px;
  border: 1px dashed rgba(255, 255, 255, 0.16);
  background: rgba(255, 255, 255, 0.04);
}
.pool-wrap {
  background: rgba(0, 0, 0, 0.16);
  border-radius: 12px;
  padding: 8px 10px 10px;
}
.pool-title {
  font-size: 12px;
  color: var(--muted);
  margin-bottom: 8px;
}
.pool {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  min-height: 76px;
  align-items: center;
}
.empty { font-size: 12px; color: var(--muted); }
.card {
  width: 48px;
  height: 68px;
  border: 0;
  border-radius: 8px;
  background: linear-gradient(180deg, #fffef8 0%, #f2efe6 100%);
  color: #1a1a1a;
  box-shadow: 0 2px 0 #c9c2b0, 0 6px 12px rgba(0, 0, 0, 0.25);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  cursor: pointer;
  padding: 0;
  transition: transform 0.12s, box-shadow 0.12s;
}
.card:hover:not(:disabled) { transform: translateY(-3px); }
.card:disabled { cursor: default; opacity: 0.85; }
.card.red { color: #c62828; }
.card.selected {
  outline: 2px solid var(--gold);
  outline-offset: 2px;
  transform: translateY(-4px);
}
.card .rank { font-size: 15px; font-weight: 800; line-height: 1; }
.card .suit { font-size: 18px; line-height: 1; }
.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.actions button {
  border: 0;
  border-radius: 8px;
  padding: 7px 10px;
  background: rgba(255, 255, 255, 0.12);
  color: var(--ink);
  cursor: pointer;
}
.actions button:disabled { opacity: 0.4; cursor: not-allowed; }
.actions .primary {
  background: linear-gradient(180deg, #f0d36a, #c9a227);
  color: #2a2108;
  font-weight: 700;
}
.actions .ghost { background: transparent; border: 1px solid rgba(255,255,255,0.2); }
.log {
  margin: 0;
  max-height: 88px;
  overflow: auto;
  font-size: 11px;
  color: #b7cfc2;
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  padding: 6px 8px;
  white-space: pre-wrap;
}
@media (max-width: 720px) {
  .duns { grid-template-columns: 1fr; }
}
</style>