<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

type GiftItem = { item_id: number; quantity: number; expire_sec: number }

type RulesForm = {
  reward_key: string
  with_currency: boolean
  currency: number
  amount: number
  with_items: boolean
  items: GiftItem[]
  target_games: number
  stock: number
  price_currency: number
  price_amount: number
}

const allItems = ref<any[]>([])
const itemDefs = ref<any[]>([])
const q = ref('')
const page = ref(1)
const pageSize = ref(20)

const form = ref({
  id: 0,
  type: 'sign',
  title: '',
  enabled: true,
  start_at: '',
  end_at: '',
})

const rules = ref<RulesForm>(defaultRules('sign'))

function defaultRules(type: string): RulesForm {
  return {
    reward_key: type === 'sign' ? 'daily' : type === 'task' ? 'games_3' : 'gift1',
    with_currency: true,
    currency: 1,
    amount: type === 'gift' ? 2000 : type === 'task' ? 10 : 500,
    with_items: false,
    items: [{ item_id: 1001, quantity: 1, expire_sec: 0 }],
    target_games: 3,
    stock: 100,
    price_currency: 2,
    price_amount: 0,
  }
}

const filtered = computed(() => {
  const key = q.value.trim().toLowerCase()
  if (!key) return allItems.value
  return allItems.value.filter((row) => {
    return (
      String(row.id).includes(key) ||
      String(row.type || '').toLowerCase().includes(key) ||
      String(row.title || '').toLowerCase().includes(key)
    )
  })
})

const total = computed(() => filtered.value.length)
const items = computed(() => {
  const start = (page.value - 1) * pageSize.value
  return filtered.value.slice(start, start + pageSize.value)
})

watch(
  () => form.value.type,
  (t, prev) => {
    if (t === prev) return
    if (form.value.id <= 0) rules.value = defaultRules(t)
  },
)

function parseRules(raw: unknown): RulesForm {
  let obj: any = {}
  if (typeof raw === 'string') {
    try {
      obj = JSON.parse(raw || '{}')
    } catch {
      obj = {}
    }
  } else if (raw && typeof raw === 'object') {
    obj = raw
  }
  const reward = obj.reward && typeof obj.reward === 'object' ? obj.reward : {}
  const price = obj.price && typeof obj.price === 'object' ? obj.price : {}
  const itemRows: GiftItem[] = Array.isArray(reward.items)
    ? reward.items.map((x: any) => ({
        item_id: Number(x.item_id) || 0,
        quantity: Number(x.quantity) || 1,
        expire_sec: Number(x.expire_sec) || 0,
      }))
    : []
  const amount = Number(reward.amount) || 0
  const currency = Number(reward.currency) || 1
  return {
    reward_key: String(obj.reward_key || 'reward'),
    with_currency: amount > 0,
    currency,
    amount: amount > 0 ? amount : 500,
    with_items: itemRows.length > 0,
    items: itemRows.length ? itemRows : [{ item_id: 1001, quantity: 1, expire_sec: 0 }],
    target_games: Number(obj.target_games) || 3,
    stock: Number(obj.stock) || 100,
    price_currency: Number(price.currency) || 2,
    price_amount: Number(price.amount) || 0,
  }
}

function buildRulesJson(): Record<string, unknown> | null {
  const rk = rules.value.reward_key.trim()
  if (!rk) {
    ElMessage.warning('请填写奖励键')
    return null
  }
  const reward: Record<string, unknown> = {}
  if (rules.value.with_currency) {
    if (!rules.value.amount || rules.value.amount <= 0) {
      ElMessage.warning('货币数量须大于 0')
      return null
    }
    reward.currency = rules.value.currency
    reward.amount = rules.value.amount
  }
  if (rules.value.with_items) {
    const rows = rules.value.items.filter((x) => x.item_id > 0 && x.quantity > 0)
    if (!rows.length) {
      ElMessage.warning('请至少配置一条有效道具')
      return null
    }
    reward.items = rows
  }
  if (!rules.value.with_currency && !rules.value.with_items) {
    ElMessage.warning('请至少配置货币或道具奖励')
    return null
  }

  const out: Record<string, unknown> = { reward_key: rk, reward }
  if (form.value.type === 'task') {
    if (rules.value.target_games <= 0) {
      ElMessage.warning('目标对局数须大于 0')
      return null
    }
    out.target_games = rules.value.target_games
  }
  if (form.value.type === 'gift') {
    if (rules.value.stock < 0) {
      ElMessage.warning('库存不能为负')
      return null
    }
    out.stock = rules.value.stock
    out.price = { currency: rules.value.price_currency, amount: rules.value.price_amount }
  }
  return out
}

function rewardSummary(row: any): string {
  const r = parseRules(row.rules_json)
  const parts: string[] = []
  if (r.with_currency) parts.push(`${r.currency === 2 ? '钻石' : '金币'}${r.amount}`)
  if (r.with_items) parts.push(r.items.map((x) => `道具${x.item_id}x${x.quantity}`).join(','))
  if (row.type === 'task') parts.unshift(`对局≥${r.target_games}`)
  if (row.type === 'gift') parts.unshift(`库存${r.stock}`)
  return parts.length ? parts.join(' · ') : '-'
}

function addItemRow() {
  rules.value.items.push({ item_id: 1001, quantity: 1, expire_sec: 0 })
}

function removeItemRow(i: number) {
  rules.value.items.splice(i, 1)
  if (!rules.value.items.length) rules.value.items.push({ item_id: 1001, quantity: 1, expire_sec: 0 })
}

async function loadItemDefs() {
  const r = await api.items()
  if (r.code === 0) itemDefs.value = (r.data.items || []).filter((x: any) => x.enabled)
}

async function load() {
  const r = await api.activities({ q: q.value.trim() || undefined })
  if (r.code === 0) {
    allItems.value = r.data.items || []
  } else {
    ElMessage.error(r.message || '加载失败')
  }
}

function search() {
  page.value = 1
  load()
}

function resetForm() {
  form.value = { id: 0, type: 'sign', title: '', enabled: true, start_at: '', end_at: '' }
  rules.value = defaultRules('sign')
}

async function save() {
  if (!form.value.title.trim()) {
    ElMessage.warning('请填写标题')
    return
  }
  const rulesObj = buildRulesJson()
  if (!rulesObj) return
  const r = await api.upsertActivity({
    id: form.value.id,
    type: form.value.type,
    title: form.value.title.trim(),
    rules_json: rulesObj,
    enabled: form.value.enabled,
    start_at: form.value.start_at || undefined,
    end_at: form.value.end_at || undefined,
  })
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  if (r.code === 0) {
    resetForm()
    load()
  }
}

function edit(row: any) {
  form.value = {
    id: row.id,
    type: row.type,
    title: row.title,
    enabled: !!row.enabled,
    start_at: row.start_at || '',
    end_at: row.end_at || '',
  }
  rules.value = parseRules(row.rules_json)
}

async function setEnabled(row: any, enabled: boolean) {
  const label = enabled ? '上架' : '下架'
  await ElMessageBox.confirm(`确认${label}活动 ${row.id}「${row.title}」?`, '二次确认')
  const r = enabled ? await api.setActivityEnabled(row.id, true) : await api.deleteActivity(row.id)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  load()
}

onMounted(async () => {
  await loadItemDefs()
  await load()
})
</script>

<template>
  <h2>活动管理</h2>
  <el-form :model="form" label-width="110px" style="max-width: 760px; margin-bottom: 20px">
    <el-form-item label="ID(0=新建)"><el-input-number v-model="form.id" /></el-form-item>
    <el-form-item label="类型">
      <el-select v-model="form.type" style="width: 180px">
        <el-option label="签到 sign" value="sign" />
        <el-option label="任务 task" value="task" />
        <el-option label="礼包 gift" value="gift" />
      </el-select>
    </el-form-item>
    <el-form-item label="标题"><el-input v-model="form.title" placeholder="活动标题" /></el-form-item>

    <el-divider content-position="left">规则与奖励</el-divider>

    <el-form-item label="奖励键">
      <el-input v-model="rules.reward_key" style="width: 220px" placeholder="如 daily / games_3" />
      <span class="hint">幂等领奖用，同活动内勿重复</span>
    </el-form-item>

    <el-form-item v-if="form.type === 'task'" label="目标对局数">
      <el-input-number v-model="rules.target_games" :min="1" />
    </el-form-item>

    <template v-if="form.type === 'gift'">
      <el-form-item label="库存">
        <el-input-number v-model="rules.stock" :min="0" />
      </el-form-item>
      <el-form-item label="购买价格">
        <el-select v-model="rules.price_currency" style="width: 110px">
          <el-option :value="1" label="金币" />
          <el-option :value="2" label="钻石" />
        </el-select>
        <el-input-number v-model="rules.price_amount" :min="0" style="margin-left: 8px" />
        <span class="hint">0 表示免费领</span>
      </el-form-item>
    </template>

    <el-form-item label="货币奖励">
      <el-switch v-model="rules.with_currency" active-text="发放" inactive-text="无" />
      <template v-if="rules.with_currency">
        <el-select v-model="rules.currency" style="width: 110px; margin-left: 12px">
          <el-option :value="1" label="金币" />
          <el-option :value="2" label="钻石" />
        </el-select>
        <el-input-number v-model="rules.amount" :min="1" style="margin-left: 8px" />
      </template>
    </el-form-item>

    <el-form-item label="道具奖励">
      <el-switch v-model="rules.with_items" active-text="发放" inactive-text="无" />
      <div v-if="rules.with_items" style="width: 100%; margin-top: 8px">
        <div
          v-for="(row, i) in rules.items"
          :key="i"
          style="display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 8px; align-items: center"
        >
          <el-select
            v-model="row.item_id"
            filterable
            allow-create
            default-first-option
            style="width: 220px"
            placeholder="道具"
          >
            <el-option
              v-for="d in itemDefs"
              :key="d.id"
              :value="d.id"
              :label="`${d.id} ${d.name} (${d.kind})`"
            />
          </el-select>
          <span>数量</span>
          <el-input-number v-model="row.quantity" :min="1" />
          <span>expire_sec</span>
          <el-input-number v-model="row.expire_sec" :min="0" />
          <el-button size="small" @click="removeItemRow(i)">删</el-button>
        </div>
        <el-button size="small" @click="addItemRow">加一条道具</el-button>
        <div class="hint" style="display: block; margin: 6px 0 0; margin-left: 0">
          expire_sec=0 用道具默认时长；ttl 为续期单份秒数
        </div>
      </div>
    </el-form-item>

    <el-form-item label="开始时间">
      <el-input v-model="form.start_at" placeholder="YYYY-MM-DD HH:mm:ss，空=不限" />
    </el-form-item>
    <el-form-item label="结束时间">
      <el-input v-model="form.end_at" placeholder="YYYY-MM-DD HH:mm:ss，空=不限" />
    </el-form-item>
    <el-form-item label="上架"><el-switch v-model="form.enabled" /></el-form-item>
    <el-form-item>
      <el-button type="primary" @click="save">保存 / 热更</el-button>
      <el-button @click="resetForm">清空表单</el-button>
    </el-form-item>
  </el-form>

  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>关键词</label>
      <el-input
        v-model="q"
        clearable
        class="admin-filter-control"
        placeholder="ID / 类型 / 标题"
        @keyup.enter="search"
      />
      <span class="admin-filter-tip">按活动 ID、类型或标题筛选</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="search">查询</el-button>
      <el-button @click="q = ''; search()">清空条件</el-button>
      <el-button @click="load">刷新</el-button>
    </div>
  </div>

  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="70" />
    <el-table-column prop="type" label="类型" width="90" />
    <el-table-column prop="title" label="标题" min-width="120" />
    <el-table-column label="奖励摘要" min-width="220" show-overflow-tooltip>
      <template #default="{ row }">{{ rewardSummary(row) }}</template>
    </el-table-column>
    <el-table-column label="奖励键" width="110" show-overflow-tooltip>
      <template #default="{ row }">{{ parseRules(row.rules_json).reward_key }}</template>
    </el-table-column>
    <el-table-column label="开始" width="160">
      <template #default="{ row }">{{ fmtTime(row.start_at) }}</template>
    </el-table-column>
    <el-table-column label="结束" width="160">
      <template #default="{ row }">{{ fmtTime(row.end_at) }}</template>
    </el-table-column>
    <el-table-column label="上架" width="80">
      <template #default="{ row }">
        <el-tag :type="row.enabled ? 'success' : 'info'" size="small">{{ row.enabled ? '是' : '否' }}</el-tag>
      </template>
    </el-table-column>
    <el-table-column prop="claim_count" label="领取次数" width="100" />
    <el-table-column label="操作" width="220">
      <template #default="{ row }">
        <el-button size="small" @click="edit(row)">编辑</el-button>
        <el-button v-if="!row.enabled" size="small" type="success" @click="setEnabled(row, true)">上架</el-button>
        <el-button v-else size="small" type="danger" @click="setEnabled(row, false)">下架</el-button>
      </template>
    </el-table-column>
  </el-table>
  <el-pagination
    style="margin-top: 12px; justify-content: flex-end"
    background
    layout="total, sizes, prev, pager, next"
    :total="total"
    v-model:current-page="page"
    v-model:page-size="pageSize"
    :page-sizes="[10, 20, 50]"
  />
</template>

<style scoped>
.hint {
  margin-left: 10px;
  color: #94a3b8;
  font-size: 12px;
}
</style>
