<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { claimActivity, fetchActivities, fetchProfile, type ActivityItem } from '../api'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const { token, gold, diamond, applyBalances, pushLog, activityTick } = useGameSession()

const items = ref<ActivityItem[]>([])
const tip = ref('')
const busy = ref(false)
let unwatch: (() => void) | null = null

async function load() {
  if (!token.value) {
    router.replace('/login')
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const r = await fetchActivities(token.value)
    if (r.code !== 0) {
      tip.value = r.message || '加载失败'
      return
    }
    items.value = r.data.items || []
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

async function claim(it: ActivityItem) {
  if (!token.value || busy.value) return
  busy.value = true
  tip.value = ''
  try {
    const r = await claimActivity(token.value, it.id, it.reward_key)
    if (r.code !== 0) {
      tip.value = r.message || '领奖失败'
      pushLog(`claim fail ${it.id}: ${tip.value}`)
      await load()
      return
    }
    if (r.data.currency === 2) applyBalances(gold.value, r.data.balance)
    else applyBalances(r.data.balance, diamond.value)
    tip.value = `领取成功：余额 ${r.data.balance}`
    pushLog(`claim ok activity=${it.id}`)
    const p = await fetchProfile(token.value)
    if (p.code === 0) applyBalances(p.data.gold, p.data.diamond)
    await load()
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

function progressText(it: ActivityItem): string {
  const p = it.progress_json || {}
  if (it.type === 'sign') {
    return p.signed_today || it.claimed ? '今日已签' : '今日未签'
  }
  if (it.type === 'task') {
    const games = Number(p.games ?? 0)
    const target = Number((it.rules_json as { target_games?: number })?.target_games ?? p.target ?? 3)
    return `${games} / ${target} 局`
  }
  if (it.type === 'gift') {
    return `库存约 ${p.stock_left ?? '?'}`
  }
  return JSON.stringify(p)
}

function progressPct(it: ActivityItem): number {
  if (it.type !== 'task') return it.claimed || !it.claimable ? 100 : 0
  const p = it.progress_json || {}
  const games = Number(p.games ?? 0)
  const target = Number((it.rules_json as { target_games?: number })?.target_games ?? p.target ?? 3)
  if (target <= 0) return 100
  return Math.min(100, Math.round((games / target) * 100))
}

onMounted(() => {
  load()
  unwatch = activityTick.subscribe(() => {
    load()
  })
})
onUnmounted(() => {
  unwatch?.()
})
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

  <section class="card">
    <h2>活动中心</h2>
    <div v-if="!items.length" class="empty">暂无活动</div>
    <div v-for="it in items" :key="it.id" class="act">
      <div class="info">
        <strong>{{ it.title }}</strong>
        <span class="meta">{{ it.type }} · {{ progressText(it) }}</span>
        <div v-if="it.type === 'task'" class="bar"><i :style="{ width: progressPct(it) + '%' }" /></div>
      </div>
      <button :disabled="busy || !it.claimable || it.claimed" @click="claim(it)">
        {{ it.claimed ? '已领' : it.claimable ? '领取' : '不可领' }}
      </button>
    </div>
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
.row { display: flex; gap: 10px; margin-top: 8px; }
.act {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 14px 0;
  border-bottom: 1px solid #eee;
}
.info { flex: 1; min-width: 0; }
.meta { display: block; color: #64748b; font-size: 13px; margin-top: 4px; }
.bar {
  margin-top: 8px;
  height: 6px;
  background: #e2e8f0;
  border-radius: 4px;
  overflow: hidden;
}
.bar i {
  display: block;
  height: 100%;
  background: #2563eb;
}
.empty { color: #94a3b8; padding: 12px 0; }
.banner {
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 12px;
  background: #eff6ff;
  color: #1d4ed8;
}
button {
  border: 0;
  background: #2563eb;
  color: #fff;
  padding: 8px 14px;
  border-radius: 6px;
  cursor: pointer;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.ghost {
  background: #e2e8f0;
  color: #334155;
}
h2 { margin: 0 0 12px; }
</style>
