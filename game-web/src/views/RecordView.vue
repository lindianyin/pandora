<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { fetchRecordRecent, fetchRecordSummary, type RecentRound, type RecordSummary } from '../api'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const { token, gold, diamond } = useGameSession()
const tip = ref('')
const busy = ref(false)
const summary = ref<RecordSummary | null>(null)
const items = ref<RecentRound[]>([])

async function load() {
  if (!token.value) {
    router.replace('/login')
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const [s, r] = await Promise.all([fetchRecordSummary(token.value), fetchRecordRecent(token.value)])
    if (s.code !== 0) {
      tip.value = s.message
      return
    }
    if (r.code !== 0) {
      tip.value = r.message
      return
    }
    summary.value = s.data
    items.value = r.data.items || []
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

onMounted(load)
</script>

<template>
  <section class="card profile">
    <div>金币 {{ gold }} / 钻石 {{ diamond }}</div>
    <div class="row">
      <button class="ghost" @click="router.push('/lobby')">返回大厅</button>
      <button class="ghost" :disabled="busy" @click="load">刷新</button>
    </div>
  </section>
  <div v-if="tip" class="banner">{{ tip }}</div>
  <section class="card" v-if="summary">
    <h2>战绩汇总</h2>
    <div class="grid">
      <div>总局 {{ summary.total_rounds }}</div>
      <div>胜 {{ summary.win_rounds }} / 负 {{ summary.lose_rounds }}</div>
      <div>胜率 {{ (summary.win_rate_bp / 100).toFixed(2) }}%</div>
      <div>地主局 {{ summary.landlord_rounds }}</div>
      <div>累计赢金 {{ summary.gold_win_sum }}</div>
      <div>累计输金 {{ summary.gold_lose_sum }}</div>
    </div>
  </section>
  <section class="card">
    <h2>近期对局</h2>
    <div v-if="!items.length" class="empty">暂无对局</div>
    <div v-for="it in items" :key="it.round_id" class="row-item">
      <div>
        <strong>#{{ it.round_id }}</strong>
        <span class="meta">{{ it.ended_at }} · 底分 {{ it.base_score }} ×{{ it.multiplier }}</span>
      </div>
      <div :class="it.delta_gold >= 0 ? 'win' : 'lose'">
        {{ it.result }} {{ it.delta_gold >= 0 ? '+' : '' }}{{ it.delta_gold }}
        <span v-if="it.is_landlord">地主</span>
      </div>
    </div>
  </section>
</template>

<style scoped>
.card { background: #fff; border: 1px solid #e5e7eb; border-radius: 10px; padding: 16px; margin-bottom: 16px; }
.profile { line-height: 1.6; font-size: 14px; }
.row { display: flex; gap: 10px; margin-top: 8px; flex-wrap: wrap; }
.grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 8px; font-size: 14px; }
.row-item { display: flex; justify-content: space-between; gap: 12px; padding: 12px 0; border-bottom: 1px solid #eee; }
.meta { display: block; color: #64748b; font-size: 13px; margin-top: 4px; }
.win { color: #15803d; } .lose { color: #b91c1c; }
.empty { color: #94a3b8; padding: 12px 0; }
.banner { padding: 8px 12px; border-radius: 6px; margin-bottom: 12px; background: #eff6ff; color: #1d4ed8; }
button { border: 0; background: #2563eb; color: #fff; padding: 8px 14px; border-radius: 6px; cursor: pointer; }
button:disabled { opacity: 0.5; }
button.ghost { background: #e2e8f0; color: #334155; }
h2 { margin: 0 0 12px; }
</style>
