<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { fetchBag, useBagItem, type BagItem } from '../api'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const { token, gold, diamond } = useGameSession()
const tip = ref('')
const busy = ref(false)
const items = ref<BagItem[]>([])
const includeExpired = ref(false)

async function load() {
  if (!token.value) {
    router.replace('/login')
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const r = await fetchBag(token.value, includeExpired.value)
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

async function useItem(it: BagItem) {
  if (!token.value || busy.value || it.kind === 'ttl' || it.quantity <= 0) return
  if (isExpired(it)) {
    tip.value = '道具已过期'
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const idem = `bag_use:${it.item_id}:${Date.now()}`
    const r = await useBagItem(token.value, {
      item_id: it.item_id,
      quantity: 1,
      expire_at: it.kind === 'qty_ttl' ? it.expire_at : undefined,
      idempotent_key: idem,
    })
    tip.value = r.code === 0 ? `已使用 ${it.name}，剩余 ${r.data.quantity_after}` : r.message
    await load()
  } finally {
    busy.value = false
  }
}

function kindLabel(kind: string) {
  if (kind === 'qty') return '数量'
  if (kind === 'qty_ttl') return '数量+时效'
  if (kind === 'ttl') return '时效续期'
  return kind
}

function isExpired(it: BagItem) {
  if (!it.expire_at || it.expire_at.startsWith('9999-')) return false
  const t = Date.parse(it.expire_at.replace(' ', 'T'))
  if (Number.isNaN(t)) return false
  return t <= Date.now()
}

function expireLabel(it: BagItem) {
  if (!it.expire_at) return '-'
  if (it.expire_at.startsWith('9999-')) return '不过期'
  if (isExpired(it)) return `已过期（${it.expire_at}）`
  if (it.kind === 'ttl') return `有效至 ${it.expire_at}`
  return it.expire_at
}

function metaLine(it: BagItem) {
  const parts = [`ID ${it.item_id}`, kindLabel(it.kind)]
  if (it.kind === 'ttl') {
    parts.push(expireLabel(it))
  } else {
    parts.push(`x${it.quantity}`, `过期 ${expireLabel(it)}`)
  }
  if (it.tag) parts.push(it.tag)
  return parts.join(' · ')
}

onMounted(load)
</script>

<template>
  <section class="card profile">
    <div>金币 {{ gold }} / 钻石 {{ diamond }}</div>
    <div class="row">
      <button class="ghost" @click="router.push('/lobby')">返回大厅</button>
      <button class="ghost" :disabled="busy" @click="load">刷新</button>
      <label class="chk">
        <input v-model="includeExpired" type="checkbox" @change="load" />
        含过期
      </label>
    </div>
  </section>
  <div v-if="tip" class="banner">{{ tip }}</div>
  <section class="card">
    <h2>背包</h2>
    <p class="hint">数量/数量+时效可使用扣减；时效续期类为持续 buff，展示有效期，再次获得会累加时间。</p>
    <div v-if="!items.length" class="empty">暂无道具</div>
    <div v-for="it in items" :key="`${it.item_id}-${it.expire_at}`" class="row-item">
      <div class="info">
        <strong>{{ it.name || `道具 ${it.item_id}` }}</strong>
        <span class="meta">{{ metaLine(it) }}</span>
      </div>
      <button
        v-if="it.kind !== 'ttl'"
        :disabled="busy || it.quantity <= 0 || isExpired(it)"
        @click="useItem(it)"
      >
        使用 1
      </button>
      <span v-else class="buff" :class="{ off: isExpired(it) }">{{ isExpired(it) ? '已过期' : '生效中' }}</span>
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
.profile {
  line-height: 1.6;
  font-size: 14px;
}
.row {
  display: flex;
  gap: 10px;
  margin-top: 8px;
  flex-wrap: wrap;
  align-items: center;
}
.chk {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #475569;
  font-size: 13px;
}
.row-item {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  padding: 14px 0;
  border-bottom: 1px solid #eee;
  align-items: center;
}
.info {
  flex: 1;
  min-width: 0;
}
.meta {
  display: block;
  color: #64748b;
  font-size: 13px;
  margin-top: 4px;
}
.hint {
  margin: 0 0 12px;
  color: #94a3b8;
  font-size: 13px;
}
.buff {
  color: #059669;
  font-size: 13px;
  white-space: nowrap;
}
.buff.off {
  color: #94a3b8;
}
.empty {
  color: #94a3b8;
  padding: 12px 0;
}
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
button:disabled {
  opacity: 0.5;
}
button.ghost {
  background: #e2e8f0;
  color: #334155;
}
h2 {
  margin: 0 0 12px;
}
</style>
