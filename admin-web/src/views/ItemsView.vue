<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
import ItemSelect from '../components/ItemSelect.vue'

type Kind = 'qty' | 'qty_ttl' | 'ttl'

const allItems = ref<any[]>([])
const q = ref('')
const kindFilter = ref<'all' | Kind>('all')
const enabledFilter = ref<'all' | 'on' | 'off'>('all')
const page = ref(1)
const pageSize = ref(20)

const form = ref({
  id: 0,
  name: '',
  icon: '',
  kind: 'qty' as Kind,
  stackable: true,
  default_expire_sec: 0,
  tag: '',
  enabled: true,
})

const grantForm = ref({
  uid: 0,
  item_id: 0,
  quantity: 1,
  expire_sec: 0,
  idempotent_key: '',
})

const bagUid = ref(0)
const bagItems = ref<any[]>([])
const ledgerUid = ref(0)
const ledgerItemId = ref<number | undefined>(undefined)
const ledgerItems = ref<any[]>([])
const ledgerTotal = ref(0)
const ledgerPage = ref(1)

const filtered = computed(() => {
  const key = q.value.trim()
  return allItems.value.filter((row) => {
    if (kindFilter.value !== 'all' && row.kind !== kindFilter.value) return false
    if (enabledFilter.value === 'on' && !row.enabled) return false
    if (enabledFilter.value === 'off' && row.enabled) return false
    if (!key) return true
    return (
      String(row.id).includes(key) ||
      String(row.name || '').includes(key) ||
      String(row.tag || '').includes(key)
    )
  })
})

const total = computed(() => filtered.value.length)
const items = computed(() => {
  const start = (page.value - 1) * pageSize.value
  return filtered.value.slice(start, start + pageSize.value)
})

const grantDef = computed(() => allItems.value.find((x) => x.id === grantForm.value.item_id) || null)
const grantKind = computed<Kind | ''>(() => (grantDef.value?.kind as Kind) || '')

watch(
  () => form.value.kind,
  (k) => {
    if (k === 'qty') {
      form.value.default_expire_sec = 0
      form.value.stackable = true
    } else if (k === 'ttl') {
      form.value.stackable = true
      if (form.value.default_expire_sec <= 0) form.value.default_expire_sec = 604800
    } else if (k === 'qty_ttl' && form.value.default_expire_sec <= 0) {
      form.value.default_expire_sec = 86400
    }
  },
)

function kindLabel(kind: string) {
  if (kind === 'qty') return '数量'
  if (kind === 'qty_ttl') return '数量+时效'
  if (kind === 'ttl') return '时效续期'
  return kind || '-'
}

function expireLabel(s: string, kind?: string) {
  if (!s) return '-'
  if (String(s).startsWith('9999-')) return '不过期'
  if (kind === 'ttl') return `有效至 ${s}`
  return s
}

function grantHint() {
  const k = grantKind.value
  if (k === 'qty') return '数量道具：只加数量，expire_sec 忽略'
  if (k === 'qty_ttl') return '数量+时效：同到期时刻叠数量；expire_sec=0 用定义默认时长（不续期）'
  if (k === 'ttl') return '时效 buff：同道具仅一行；数量=续期份数；expire_sec=单份秒数（0=默认）；未过期则累加到期'
  return '选择道具后显示发放说明'
}

async function load() {
  const r = await api.items()
  if (r.code === 0) allItems.value = r.data.items || []
  else ElMessage.error(r.message || '加载失败')
}

function search() {
  page.value = 1
}

function edit(row: any) {
  form.value = {
    id: row.id,
    name: row.name,
    icon: row.icon || '',
    kind: row.kind,
    stackable: !!row.stackable,
    default_expire_sec: row.default_expire_sec || 0,
    tag: row.tag || '',
    enabled: !!row.enabled,
  }
}

async function save() {
  if (!form.value.name.trim()) {
    ElMessage.warning('请填写名称')
    return
  }
  const body = { ...form.value }
  if (body.kind === 'qty') {
    body.default_expire_sec = 0
    body.stackable = true
  }
  if (body.kind === 'ttl') body.stackable = true
  const r = await api.upsertItem(body)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || (r.code === 0 ? '已保存' : '失败'))
  if (r.code === 0) {
    form.value = {
      id: 0,
      name: '',
      icon: '',
      kind: 'qty',
      stackable: true,
      default_expire_sec: 0,
      tag: '',
      enabled: true,
    }
    load()
  }
}

async function setEnabled(row: any, enabled: boolean) {
  const label = enabled ? '上架' : '下架'
  await ElMessageBox.confirm(`确认${label}道具 ${row.id} ${row.name}?`)
  const r = await api.setItemEnabled(row.id, enabled)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  load()
}

async function doGrant() {
  if (!grantForm.value.uid || !grantForm.value.item_id || grantForm.value.quantity <= 0) {
    ElMessage.warning('请填写 UID / 道具 / 数量')
    return
  }
  const k = grantKind.value || '?'
  const qtyLabel = k === 'ttl' ? `续期 ${grantForm.value.quantity} 份` : `x${grantForm.value.quantity}`
  await ElMessageBox.confirm(
    `确认发放 ${kindLabel(k)} 道具 ${grantForm.value.item_id}（${qtyLabel}）给 UID ${grantForm.value.uid}?`,
  )
  const body: any = {
    uid: grantForm.value.uid,
    item_id: grantForm.value.item_id,
    quantity: grantForm.value.quantity,
  }
  if (k !== 'qty' && grantForm.value.expire_sec > 0) body.expire_sec = grantForm.value.expire_sec
  body.idempotent_key =
    grantForm.value.idempotent_key.trim() ||
    `admin_grant:${grantForm.value.uid}:${grantForm.value.item_id}:${Date.now()}`
  const r = await api.bagGrant(body)
  if (r.code === 0) {
    const exp = (r.data as any)?.expire_at
    ElMessage.success(
      k === 'ttl'
        ? `续期成功${exp ? '，有效至 ' + exp : ''}`
        : `发放成功，quantity_after=${(r.data as any)?.quantity_after ?? '-'}`,
    )
    if (bagUid.value === grantForm.value.uid) loadBag()
  } else {
    ElMessage.error(r.message || '失败')
  }
}

async function loadBag() {
  if (!bagUid.value) {
    ElMessage.warning('请填写 UID')
    return
  }
  const r = await api.playerBag(bagUid.value, true)
  if (r.code === 0) bagItems.value = r.data.items || []
  else ElMessage.error(r.message || '查询失败')
}

async function loadLedgers() {
  const r = await api.itemLedgers({
    uid: ledgerUid.value || undefined,
    item_id: ledgerItemId.value || undefined,
    page: ledgerPage.value,
    page_size: 20,
  })
  if (r.code === 0) {
    ledgerItems.value = r.data.items || []
    ledgerTotal.value = r.data.total || 0
  } else ElMessage.error(r.message || '查询失败')
}

onMounted(load)
</script>

<template>
  <h2>道具定义</h2>
  <p style="color: #64748b; margin-top: -8px; margin-bottom: 12px; font-size: 13px">
    qty=只计数量；qty_ttl=按到期堆叠数量（不续期）；ttl=同道具一行，再发累加有效期
  </p>
  <el-form :inline="true" :model="form" style="margin-bottom: 12px">
    <el-form-item label="ID(0=新建)"><el-input-number v-model="form.id" /></el-form-item>
    <el-form-item label="名称"><el-input v-model="form.name" style="width: 140px" /></el-form-item>
    <el-form-item label="类型">
      <el-select v-model="form.kind" style="width: 160px">
        <el-option value="qty" label="数量 qty" />
        <el-option value="qty_ttl" label="数量+时效 qty_ttl" />
        <el-option value="ttl" label="时效续期 ttl" />
      </el-select>
    </el-form-item>
    <el-form-item v-if="form.kind !== 'qty'" label="默认时长秒">
      <el-input-number v-model="form.default_expire_sec" :min="0" />
    </el-form-item>
    <el-form-item label="tag"><el-input v-model="form.tag" style="width: 100px" /></el-form-item>
    <el-form-item v-if="form.kind === 'qty_ttl'" label="可堆叠"><el-switch v-model="form.stackable" /></el-form-item>
    <el-form-item label="上架"><el-switch v-model="form.enabled" /></el-form-item>
    <el-button type="primary" @click="save">保存</el-button>
  </el-form>

  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>关键词</label>
      <el-input
        v-model="q"
        clearable
        class="admin-filter-control"
        placeholder="ID / 名称 / tag"
        @input="search"
      />
      <span class="admin-filter-tip">按道具 ID、名称或标签筛选</span>
    </div>
    <div class="admin-filter-field">
      <label>道具类型</label>
      <el-select v-model="kindFilter" @change="search">
        <el-option value="all" label="全部类型" />
        <el-option value="qty" label="数量" />
        <el-option value="qty_ttl" label="数量+时效" />
        <el-option value="ttl" label="时效buff" />
      </el-select>
      <span class="admin-filter-tip">&nbsp;</span>
    </div>
    <div class="admin-filter-field">
      <label>上架状态</label>
      <el-select v-model="enabledFilter" @change="search">
        <el-option value="all" label="全部" />
        <el-option value="on" label="已上架" />
        <el-option value="off" label="已下架" />
      </el-select>
      <span class="admin-filter-tip">&nbsp;</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="search">查询</el-button>
      <el-button @click="load">刷新</el-button>
    </div>
  </div>

  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="80" />
    <el-table-column prop="name" label="名称" />
    <el-table-column label="类型" width="120">
      <template #default="{ row }">{{ kindLabel(row.kind) }}</template>
    </el-table-column>
    <el-table-column label="默认时长秒" width="120">
      <template #default="{ row }">{{ row.kind === 'qty' ? '-' : row.default_expire_sec }}</template>
    </el-table-column>
    <el-table-column prop="tag" label="tag" width="100" />
    <el-table-column label="上架" width="90">
      <template #default="{ row }">
        <el-tag :type="row.enabled ? 'success' : 'info'" size="small">{{ row.enabled ? '是' : '否' }}</el-tag>
      </template>
    </el-table-column>
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

  <h2 style="margin-top: 32px">人工发放</h2>
  <p style="color: #64748b; font-size: 13px; margin-top: -4px">{{ grantHint() }}</p>
  <el-form :inline="true" :model="grantForm">
    <el-form-item label="UID"><el-input-number v-model="grantForm.uid" :min="1" /></el-form-item>
    <el-form-item label="道具">
      <ItemSelect v-model="grantForm.item_id" width="240px" placeholder="选择道具" />
    </el-form-item>
    <el-form-item :label="grantKind === 'ttl' ? '续期份数' : '数量'">
      <el-input-number v-model="grantForm.quantity" :min="1" />
    </el-form-item>
    <el-form-item v-if="grantKind !== 'qty'" :label="grantKind === 'ttl' ? '单份秒数' : 'expire_sec'">
      <el-input-number v-model="grantForm.expire_sec" :min="0" />
    </el-form-item>
    <el-form-item label="幂等键"><el-input v-model="grantForm.idempotent_key" style="width: 220px" placeholder="可空自动生成" /></el-form-item>
    <el-button type="warning" @click="doGrant">发放</el-button>
  </el-form>

  <h2 style="margin-top: 32px">玩家背包</h2>
  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>玩家 UID</label>
      <el-input-number v-model="bagUid" :min="1" controls-position="right" placeholder="玩家 UID" />
      <span class="admin-filter-tip">查询该玩家当前背包道具</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="loadBag">查询</el-button>
    </div>
  </div>
  <el-table :data="bagItems" border>
    <el-table-column prop="item_id" label="道具" width="90" />
    <el-table-column prop="name" label="名称" />
    <el-table-column label="类型" width="110">
      <template #default="{ row }">{{ kindLabel(row.kind) }}</template>
    </el-table-column>
    <el-table-column label="数量" width="90">
      <template #default="{ row }">{{ row.kind === 'ttl' ? '持有' : row.quantity }}</template>
    </el-table-column>
    <el-table-column label="过期/有效至" min-width="200">
      <template #default="{ row }">{{ expireLabel(row.expire_at, row.kind) }}</template>
    </el-table-column>
    <el-table-column prop="tag" label="tag" width="100" />
  </el-table>

  <h2 style="margin-top: 32px">道具流水</h2>
  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>玩家 UID</label>
      <el-input-number
        v-model="ledgerUid"
        :min="0"
        controls-position="right"
        placeholder="0 表示不限"
      />
      <span class="admin-filter-tip">填 0 或不改则查全部玩家</span>
    </div>
    <div class="admin-filter-field">
      <label>道具</label>
      <ItemSelect
        v-model="ledgerItemId"
        clearable
        include-disabled
        placeholder="全部道具"
      />
      <span class="admin-filter-tip">不选则不按道具过滤</span>
    </div>
    <div class="admin-filter-actions">
      <el-button
        type="primary"
        @click="
          () => {
            ledgerPage = 1
            loadLedgers()
          }
        "
        >查询</el-button
      >
      <el-button
        @click="
          () => {
            ledgerUid = 0
            ledgerItemId = undefined
            ledgerPage = 1
            loadLedgers()
          }
        "
        >清空条件</el-button
      >
    </div>
  </div>
  <el-table :data="ledgerItems" border>
    <el-table-column prop="id" label="流水 ID" width="90" />
    <el-table-column prop="uid" label="玩家 UID" width="100" />
    <el-table-column prop="item_id" label="道具 ID" width="90" />
    <el-table-column prop="delta" label="变动数量" width="100" />
    <el-table-column prop="quantity_after" label="变动后数量" width="110" />
    <el-table-column prop="expire_at" label="过期/有效至" min-width="170" />
    <el-table-column prop="biz_type" label="业务类型" width="120" show-overflow-tooltip />
    <el-table-column prop="idempotent_key" label="幂等键" min-width="180" show-overflow-tooltip />
    <el-table-column prop="created_at" label="时间" min-width="170" />
  </el-table>
  <el-pagination
    style="margin-top: 12px; justify-content: flex-end"
    background
    layout="total, prev, pager, next"
    :total="ledgerTotal"
    v-model:current-page="ledgerPage"
    :page-size="20"
    @current-change="loadLedgers"
  />
</template>
