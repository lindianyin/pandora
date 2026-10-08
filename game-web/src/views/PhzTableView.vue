<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
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

onUnmounted(() => {
  /* socket closed when page leaves via GC of composable refs; leave room optional */
})
</script>

<template>
  <section class="page">
    <header class="bar">
      <h1>跑胡子桌</h1>
      <button class="ghost" @click="router.push('/lobby')">回大厅</button>
      <button class="ghost" @click="router.push('/phz-lab')">三联调试</button>
    </header>
    <p v-if="c.errorBanner.value" class="err">{{ c.errorBanner.value }}</p>
    <div v-if="c.phzRoundId.value" class="meta">
      局号 {{ c.phzRoundId.value }} · 庄 seat{{ c.phzBanker.value }} · 墩 {{ c.phzWall.value }} ·
      {{ c.phzSub.value }} · {{ c.countdown.value }}s
    </div>
    <div v-if="c.phzReveal.value" class="reveal">
      示众 seat{{ c.phzReveal.value.seat }}：
      <span class="tile" :class="{ red: phzIsRed(c.phzReveal.value.tile) }">{{
        c.phzTileLabel(c.phzReveal.value.tile)
      }}</span>
    </div>
    <div class="melds">
      <div v-for="s in 3" :key="'m' + s" class="meld-seat">
        <span class="muted">seat{{ s - 1 }}</span>
        <span
          v-for="(m, mi) in c.phzMelds.value[s - 1] || []"
          :key="mi"
          class="meld-group"
          :class="{ dark: isDarkMeld(m.kind) }"
        >
          {{ meldKindLabel(m.kind) }}
          <span v-for="(t, ti) in m.tiles" :key="ti" class="tile sm" :class="{ red: phzIsRed(t) }">{{
            c.phzTileLabel(t)
          }}</span>
        </span>
      </div>
    </div>
    <div class="hand">
      <button
        v-for="(t, i) in c.hand.value"
        :key="i + '-' + t"
        class="tile"
        :class="{ on: c.selectedHandIndex.value === i, red: phzIsRed(t) }"
        @click="c.selectTile(i)"
      >
        {{ c.phzTileLabel(t) }}
      </button>
    </div>
    <div class="row" v-if="c.isDiscardTurn.value || c.isClaimTurn.value">
      <button v-if="c.isDiscardTurn.value" :disabled="c.selectedHandIndex.value == null" @click="c.discardSelected()">
        出牌
      </button>
      <template v-if="c.isClaimTurn.value">
        <button @click="c.claim(0)">过</button>
        <button v-if="c.canClaimChi.value" @click="c.claim(1)">吃</button>
        <button v-if="c.canClaimPeng.value" @click="c.claim(2)">碰</button>
        <button v-if="c.phzCanHu.value" @click="c.claim(4)">胡</button>
      </template>
    </div>
    <div v-if="c.showSettle.value" class="settle">
      <template v-if="c.settle.value">
        <p>
          结算 · 息{{ c.settle.value.hu_xi }} 囤{{ c.settle.value.tun }} 番{{ c.settle.value.fan }} ·
          {{ c.settleMingTang.value }}
        </p>
        <p v-for="e in c.settle.value.entries" :key="e.seat_id">seat{{ e.seat_id }} {{ e.delta_gold }}</p>
      </template>
      <p v-else>流局</p>
      <button @click="c.dismissSettle()">关闭</button>
    </div>
    <ul class="logs">
      <li v-for="(line, i) in c.logs.value" :key="i">{{ line }}</li>
    </ul>
  </section>
</template>

<style scoped>
.page {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.bar {
  display: flex;
  gap: 8px;
  align-items: center;
}
.bar h1 {
  margin: 0;
  font-size: 20px;
  flex: 1;
}
.meta {
  font-size: 13px;
}
.reveal {
  background: #fff7ed;
  padding: 6px 8px;
  border-radius: 6px;
}
.hand {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.tile {
  min-width: 36px;
  padding: 8px;
  border: 1px solid #98a2b3;
  border-radius: 6px;
  background: #fff;
  font-weight: 600;
  cursor: pointer;
}
.tile.sm {
  min-width: 24px;
  padding: 2px 4px;
  font-size: 12px;
  cursor: default;
}
.tile.on {
  outline: 2px solid #1570ef;
}
.tile.red {
  color: #d92d20;
}
.meld-group {
  display: inline-flex;
  gap: 2px;
  margin-right: 8px;
  padding: 2px 4px;
  background: #eef2f6;
  border-radius: 4px;
}
.meld-group.dark {
  background: #d0d5dd;
}
.row {
  display: flex;
  gap: 6px;
}
.settle {
  background: #ecfdf3;
  padding: 8px;
  border-radius: 8px;
}
.err {
  color: #b42318;
}
.muted {
  color: #667085;
  font-size: 12px;
}
.logs {
  list-style: none;
  margin: 0;
  padding: 0;
  font-size: 12px;
  max-height: 160px;
  overflow: auto;
}
</style>
