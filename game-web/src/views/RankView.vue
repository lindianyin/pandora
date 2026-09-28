<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { fetchRank, type RankEntry } from '../api'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const { token, gold, diamond } = useGameSession()
const tip = ref('')
const busy = ref(false)
const period = ref<'daily' | 'weekly'>('daily')
const list = ref<RankEntry[]>([])
const me = ref({ rank: 0, score: 0 })
const periodKey = ref('')

async function load() {
  if (!token.value) {
    router.replace('/login')
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const r = await fetchRank(token.value, period.value)
    if (r.code !== 0) {
      tip.value = r.message
      return
    }
    list.value = r.data.list || []
    me.value = r.data.me || { rank: 0, score: 0 }
    periodKey.value = r.data.period_key || ''
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

function switchPeriod(p: 'daily' | 'weekly') {
  period.value = p
  load()
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
  <section class="card">
    <div class="head">
      <h2>排行榜 · {{ periodKey }}</h2>
      <div class="row">
        <button :class="{ ghost: period !== 'daily' }" @click="switchPeriod('daily')">日榜</button>
        <button :class="{ ghost: period !== 'weekly' }" @click="switchPeriod('weekly')">周榜</button>
      </div>
    </div>
    <div class="me">我的名次：{{ me.rank || '未上榜' }} · 分数 {{ me.score }}</div>
    <div v-if="!list.length" class="empty">暂无数据</div>
    <div v-for="it in list" :key="it.uid" class="row-item">
      <div>
        <strong>#{{ it.rank }} {{ it.nickname }}</strong>
        <span class="meta">UID {{ it.uid }}</span>
      </div>
      <div>{{ it.score }}</div>
    </div>
  </section>
</template>

<style scoped>
.card { background: #fff; border: 1px solid #e5e7eb; border-radius: 10px; padding: 16px; margin-bottom: 16px; }
.profile { line-height: 1.6; font-size: 14px; }
.row { display: flex; gap: 10px; margin-top: 8px; flex-wrap: wrap; }
.head { display: flex; justify-content: space-between; align-items: center; gap: 12px; flex-wrap: wrap; }
.head h2 { margin: 0; }
.me { margin: 12px 0; color: #334155; font-size: 14px; }
.row-item { display: flex; justify-content: space-between; gap: 12px; padding: 12px 0; border-bottom: 1px solid #eee; }
.meta { display: block; color: #64748b; font-size: 13px; margin-top: 4px; }
.empty { color: #94a3b8; padding: 12px 0; }
.banner { padding: 8px 12px; border-radius: 6px; margin-bottom: 12px; background: #eff6ff; color: #1d4ed8; }
button { border: 0; background: #2563eb; color: #fff; padding: 8px 14px; border-radius: 6px; cursor: pointer; }
button:disabled { opacity: 0.5; }
button.ghost { background: #e2e8f0; color: #334155; }
</style>
