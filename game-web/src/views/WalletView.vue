<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import {
  createAlipayOrder,
  exchangeDiamond,
  fetchPayProducts,
  fetchProfile,
  sandboxComplete,
  type PayProduct,
} from '../api'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const { token, gold, diamond, uid, nickname, wsOk, applyBalances, pushLog, errorBanner } =
  useGameSession()

const busy = ref(false)
const tip = ref('')
const exchangeAmt = ref(10)
const products = ref<PayProduct[]>([])
const sandbox = ref(true)
const lastOrderId = ref('')
const rate = ref(1000)

async function refresh() {
  if (!token.value) {
    router.replace('/login')
    return
  }
  busy.value = true
  tip.value = ''
  try {
    const p = await fetchProfile(token.value)
    if (p.code === 0) {
      applyBalances(p.data.gold, p.data.diamond)
    } else {
      tip.value = p.message || '拉取资料失败'
    }
    const pr = await fetchPayProducts(token.value)
    if (pr.code === 0) {
      products.value = pr.data.items || []
      sandbox.value = !!pr.data.sandbox
    }
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

async function doExchange() {
  if (!token.value || exchangeAmt.value <= 0) return
  busy.value = true
  tip.value = ''
  try {
    const orderId = `web-ex-${Date.now()}`
    const r = await exchangeDiamond(token.value, exchangeAmt.value, orderId)
    if (r.code !== 0) {
      tip.value = r.message || '兑换失败'
      return
    }
    applyBalances(r.data.gold, r.data.diamond ?? diamond.value - exchangeAmt.value)
    if (typeof (r.data as { rate?: number }).rate === 'number') {
      rate.value = (r.data as { rate: number }).rate
    }
    tip.value = `兑换成功：-${exchangeAmt.value} 钻 → 金币 ${r.data.gold}`
    pushLog(tip.value)
    // idempotent retry demo
    const r2 = await exchangeDiamond(token.value, exchangeAmt.value, orderId)
    if (r2.code === 0 && r2.data.gold === r.data.gold) {
      tip.value += '（幂等复用未双扣）'
    }
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

async function buy(productId: number) {
  if (!token.value) return
  busy.value = true
  tip.value = ''
  try {
    const r = await createAlipayOrder(token.value, productId)
    if (r.code !== 0) {
      tip.value = r.message || '下单失败'
      return
    }
    lastOrderId.value = r.data.order_id
    tip.value = `已下单 ${r.data.order_id}，应付 ${(r.data.amount_fen / 100).toFixed(2)} 元`
    pushLog(`pay create ${r.data.order_id} orderStr=${r.data.alipay_order_str.slice(0, 40)}…`)
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

async function doSandboxComplete() {
  if (!token.value || !lastOrderId.value) {
    tip.value = '请先下单'
    return
  }
  busy.value = true
  try {
    const r = await sandboxComplete(token.value, lastOrderId.value)
    if (r.code !== 0) {
      tip.value = r.message || '模拟到账失败'
      return
    }
    tip.value = `沙箱到账成功 order=${lastOrderId.value}`
    pushLog(tip.value)
    await refresh()
  } catch (e) {
    tip.value = String(e)
  } finally {
    busy.value = false
  }
}

onMounted(() => {
  if (!token.value) {
    router.replace('/login')
    return
  }
  refresh()
})
</script>

<template>
  <section class="card profile">
    <div>UID: {{ uid }} · {{ nickname }} · 金币 {{ gold }} / 钻石 {{ diamond }}</div>
    <div>WSS: {{ wsOk ? '已鉴权' : '未鉴权' }} · 兑换比率 1 钻 = {{ rate }} 金</div>
    <div class="row">
      <button class="ghost" @click="router.push('/lobby')">回大厅</button>
      <button class="ghost" :disabled="busy" @click="refresh">刷新余额</button>
    </div>
  </section>

  <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>
  <div v-if="tip" class="banner">{{ tip }}</div>

  <section class="card">
    <h2>钻石兑换金币</h2>
    <div class="row">
      <label>
        钻石数量
        <input v-model.number="exchangeAmt" type="number" min="1" />
      </label>
      <button :disabled="busy || exchangeAmt <= 0 || diamond < exchangeAmt" @click="doExchange">
        兑换
      </button>
    </div>
    <p class="hint">预计获得 {{ exchangeAmt * rate }} 金币（同 client_order_id 幂等）</p>
  </section>

  <section class="card">
    <h2>充值档位 {{ sandbox ? '（沙箱）' : '' }}</h2>
    <div v-if="!products.length" class="empty">暂无档位</div>
    <div v-for="p in products" :key="p.id" class="prod">
      <div>
        <strong>{{ (p.amount_fen / 100).toFixed(2) }} 元</strong>
        <span class="meta">
          {{ p.diamond }} 钻
          <template v-if="p.gift_diamond"> +赠 {{ p.gift_diamond }}</template>
          <template v-if="p.gift_items?.length">
            · 赠道具
            {{ p.gift_items.map((x) => `${x.item_id}x${x.quantity}`).join(',') }}
          </template>
        </span>
      </div>
      <button :disabled="busy" @click="buy(p.id)">下单</button>
    </div>
    <div v-if="sandbox" class="row sandbox">
      <span v-if="lastOrderId">最近订单：{{ lastOrderId }}</span>
      <button :disabled="busy || !lastOrderId" @click="doSandboxComplete">模拟到账</button>
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
  align-items: center;
  flex-wrap: wrap;
  margin-top: 10px;
}
input {
  width: 100px;
  margin-left: 8px;
  padding: 6px 8px;
  border: 1px solid #d1d5db;
  border-radius: 6px;
}
button {
  background: #2563eb;
  color: #fff;
  border: none;
  border-radius: 8px;
  padding: 8px 14px;
  cursor: pointer;
}
button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
button.ghost {
  background: #f3f4f6;
  color: #111;
}
.prod {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid #f3f4f6;
}
.meta {
  margin-left: 10px;
  color: #6b7280;
  font-size: 13px;
}
.hint,
.empty {
  color: #6b7280;
  font-size: 13px;
}
.banner {
  background: #ecfdf5;
  border: 1px solid #a7f3d0;
  color: #065f46;
  padding: 10px 12px;
  border-radius: 8px;
  margin-bottom: 12px;
}
.banner.err {
  background: #fef2f2;
  border-color: #fecaca;
  color: #991b1b;
}
.sandbox {
  margin-top: 14px;
  padding-top: 10px;
  border-top: 1px dashed #e5e7eb;
}
</style>
