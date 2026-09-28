<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import {
  fetchFriends,
  friendAccept,
  friendReject,
  friendRemove,
  friendRequest,
  type FriendItem,
} from '../api'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const { token, gold, diamond } = useGameSession()
const tip = ref('')
const busy = ref(false)
const friends = ref<FriendItem[]>([])
const incoming = ref<Array<{ from_uid: number; nickname: string }>>([])
const toUid = ref('')

async function load() {
  if (!token.value) {
    router.replace('/login')
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const r = await fetchFriends(token.value)
    if (r.code !== 0) {
      tip.value = r.message
      return
    }
    friends.value = r.data.items || []
    incoming.value = r.data.pending?.incoming || []
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

async function request() {
  if (!token.value || busy.value) return
  const uid = Number(toUid.value)
  if (!uid) {
    tip.value = '请输入对方 UID'
    return
  }
  busy.value = true
  try {
    const r = await friendRequest(token.value, uid)
    tip.value = r.code === 0 ? '已发送申请' : r.message
    await load()
  } finally {
    busy.value = false
  }
}

async function accept(fromUid: number) {
  if (!token.value) return
  const r = await friendAccept(token.value, fromUid)
  tip.value = r.code === 0 ? '已同意' : r.message
  await load()
}

async function reject(fromUid: number) {
  if (!token.value) return
  const r = await friendReject(token.value, fromUid)
  tip.value = r.code === 0 ? '已拒绝' : r.message
  await load()
}

async function remove(uid: number) {
  if (!token.value) return
  const r = await friendRemove(token.value, uid)
  tip.value = r.code === 0 ? '已删除' : r.message
  await load()
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
    <h2>添加好友</h2>
    <div class="row">
      <input v-model="toUid" placeholder="对方 UID" />
      <button :disabled="busy" @click="request">发送申请</button>
    </div>
  </section>
  <section class="card" v-if="incoming.length">
    <h2>待处理申请</h2>
    <div v-for="it in incoming" :key="it.from_uid" class="row-item">
      <div>{{ it.nickname }} ({{ it.from_uid }})</div>
      <div class="row">
        <button @click="accept(it.from_uid)">同意</button>
        <button class="ghost" @click="reject(it.from_uid)">拒绝</button>
      </div>
    </div>
  </section>
  <section class="card">
    <h2>好友列表</h2>
    <div v-if="!friends.length" class="empty">暂无好友</div>
    <div v-for="f in friends" :key="f.uid" class="row-item">
      <div>
        <strong>{{ f.nickname }}</strong>
        <span class="meta">UID {{ f.uid }} · {{ f.online ? '在线' : '离线' }}</span>
      </div>
      <button class="ghost" @click="remove(f.uid)">删除</button>
    </div>
  </section>
</template>

<style scoped>
.card { background: #fff; border: 1px solid #e5e7eb; border-radius: 10px; padding: 16px; margin-bottom: 16px; }
.profile { line-height: 1.6; font-size: 14px; }
.row { display: flex; gap: 10px; margin-top: 8px; flex-wrap: wrap; align-items: center; }
.row-item { display: flex; justify-content: space-between; gap: 12px; padding: 12px 0; border-bottom: 1px solid #eee; align-items: center; }
.meta { display: block; color: #64748b; font-size: 13px; margin-top: 4px; }
.empty { color: #94a3b8; padding: 12px 0; }
.banner { padding: 8px 12px; border-radius: 6px; margin-bottom: 12px; background: #eff6ff; color: #1d4ed8; }
input { border: 1px solid #cbd5e1; border-radius: 6px; padding: 8px 10px; min-width: 160px; }
button { border: 0; background: #2563eb; color: #fff; padding: 8px 14px; border-radius: 6px; cursor: pointer; }
button:disabled { opacity: 0.5; }
button.ghost { background: #e2e8f0; color: #334155; }
h2 { margin: 0 0 12px; }
</style>
