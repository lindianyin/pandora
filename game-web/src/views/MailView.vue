<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { fetchMails, fetchProfile, mailClaim, mailDelete, mailRead, type MailItem } from '../api'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const { token, gold, diamond, applyBalances } = useGameSession()
const tip = ref('')
const busy = ref(false)
const items = ref<MailItem[]>([])

async function load() {
  if (!token.value) {
    router.replace('/login')
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const r = await fetchMails(token.value)
    if (r.code !== 0) {
      tip.value = r.message
      return
    }
    items.value = r.data.items || []
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

async function read(it: MailItem) {
  if (!token.value || it.status !== 0) return
  await mailRead(token.value, it.id)
  await load()
}

async function claim(it: MailItem) {
  if (!token.value || busy.value) return
  busy.value = true
  try {
    const r = await mailClaim(token.value, it.id)
    if (r.code !== 0) {
      tip.value = r.message
      return
    }
    tip.value = `领取成功，余额 ${r.balance}`
    const p = await fetchProfile(token.value)
    if (p.code === 0) applyBalances(p.data.gold, p.data.diamond)
    await load()
  } finally {
    busy.value = false
  }
}

async function del(it: MailItem) {
  if (!token.value) return
  const r = await mailDelete(token.value, it.id)
  tip.value = r.code === 0 ? '已删除' : r.message
  await load()
}

function attachText(it: MailItem) {
  const a = it.attach_json || {}
  if (!a.amount) return '无附件'
  return `${a.currency === 2 ? '钻石' : '金币'} +${a.amount}`
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
    <h2>邮件</h2>
    <div v-if="!items.length" class="empty">暂无邮件</div>
    <div v-for="it in items" :key="it.id" class="mail" @click="read(it)">
      <div class="info">
        <strong>{{ it.title }}</strong>
        <span class="meta">{{ attachText(it) }} · 状态 {{ it.status }} · {{ it.created_at }}</span>
        <p>{{ it.body }}</p>
      </div>
      <div class="row">
        <button :disabled="busy || it.status === 2 || !(it.attach_json && it.attach_json.amount)" @click.stop="claim(it)">
          领取
        </button>
        <button class="ghost" @click.stop="del(it)">删除</button>
      </div>
    </div>
  </section>
</template>

<style scoped>
.card { background: #fff; border: 1px solid #e5e7eb; border-radius: 10px; padding: 16px; margin-bottom: 16px; }
.profile { line-height: 1.6; font-size: 14px; }
.row { display: flex; gap: 10px; margin-top: 8px; flex-wrap: wrap; }
.mail { display: flex; justify-content: space-between; gap: 12px; padding: 14px 0; border-bottom: 1px solid #eee; }
.info { flex: 1; min-width: 0; }
.meta { display: block; color: #64748b; font-size: 13px; margin-top: 4px; }
p { margin: 8px 0 0; color: #334155; font-size: 14px; }
.empty { color: #94a3b8; padding: 12px 0; }
.banner { padding: 8px 12px; border-radius: 6px; margin-bottom: 12px; background: #eff6ff; color: #1d4ed8; }
button { border: 0; background: #2563eb; color: #fff; padding: 8px 14px; border-radius: 6px; cursor: pointer; }
button:disabled { opacity: 0.5; }
button.ghost { background: #e2e8f0; color: #334155; }
h2 { margin: 0 0 12px; }
</style>
