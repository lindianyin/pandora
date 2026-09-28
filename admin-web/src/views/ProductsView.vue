<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'

type GiftItem = { item_id: number; quantity: number; expire_sec: number }

const allItems = ref<any[]>([])
const q = ref('')
const enabledFilter = ref<'all' | 'on' | 'off'>('all')
const page = ref(1)
const pageSize = ref(20)
const form = ref({
  id: 0,
  amount_fen: 600,
  diamond: 60,
  gift_diamond: 0,
  gift_items: [] as GiftItem[],
  enabled: true,
})

const filtered = computed(() => {
  const key = q.value.trim()
  return allItems.value.filter((row) => {
    if (enabledFilter.value === 'on' && !row.enabled) return false
    if (enabledFilter.value === 'off' && row.enabled) return false
    if (!key) return true
    return (
      String(row.id).includes(key) ||
      String(row.amount_fen).includes(key) ||
      String(row.diamond).includes(key)
    )
  })
})

const total = computed(() => filtered.value.length)
const items = computed(() => {
  const start = (page.value - 1) * pageSize.value
  return filtered.value.slice(start, start + pageSize.value)
})

function formatGifts(row: any): string {
  const arr = Array.isArray(row.gift_items) ? row.gift_items : []
  if (!arr.length) return '-'
  return arr.map((x: any) => `${x.item_id}x${x.quantity}`).join(', ')
}

function addGiftRow() {
  form.value.gift_items.push({ item_id: 1001, quantity: 1, expire_sec: 0 })
}

function removeGiftRow(i: number) {
  form.value.gift_items.splice(i, 1)
}

async function load() {
  const r = await api.products()
  if (r.code === 0) allItems.value = r.data.items || []
  else ElMessage.error(r.message || '加载失败')
}

function search() {
  page.value = 1
}

async function save() {
  const gift_items = form.value.gift_items.filter((x) => x.item_id > 0 && x.quantity > 0)
  const r = await api.upsertProduct({
    id: form.value.id,
    amount_fen: form.value.amount_fen,
    diamond: form.value.diamond,
    gift_diamond: form.value.gift_diamond,
    gift_items,
    enabled: form.value.enabled,
  })
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  if (r.code === 0) {
    form.value = { id: 0, amount_fen: 600, diamond: 60, gift_diamond: 0, gift_items: [], enabled: true }
    load()
  }
}

function edit(row: any) {
  form.value = {
    id: row.id,
    amount_fen: row.amount_fen,
    diamond: row.diamond,
    gift_diamond: row.gift_diamond,
    gift_items: Array.isArray(row.gift_items)
      ? row.gift_items.map((x: any) => ({
          item_id: Number(x.item_id) || 0,
          quantity: Number(x.quantity) || 1,
          expire_sec: Number(x.expire_sec) || 0,
        }))
      : [],
    enabled: !!row.enabled,
  }
}

async function setEnabled(row: any, enabled: boolean) {
  const label = enabled ? '上架' : '下架'
  await ElMessageBox.confirm(`确认${label}档位 ${row.id}?`)
  const r = await api.setProductEnabled(row.id, enabled)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  load()
}

onMounted(load)
</script>
<template>
  <h2>充值档位</h2>
  <el-form :inline="true" :model="form" style="margin-bottom: 12px">
    <el-form-item label="ID(0=新建)"><el-input-number v-model="form.id" /></el-form-item>
    <el-form-item label="分"><el-input-number v-model="form.amount_fen" /></el-form-item>
    <el-form-item label="钻"><el-input-number v-model="form.diamond" /></el-form-item>
    <el-form-item label="赠钻"><el-input-number v-model="form.gift_diamond" /></el-form-item>
    <el-form-item label="上架"><el-switch v-model="form.enabled" /></el-form-item>
    <el-button type="primary" @click="save">保存</el-button>
  </el-form>
  <div style="margin-bottom: 16px">
    <div style="margin-bottom: 8px; color: #64748b">赠送道具（可空）</div>
    <div v-for="(row, i) in form.gift_items" :key="i" style="display: flex; gap: 8px; margin-bottom: 8px; align-items: center">
      <span>道具ID</span>
      <el-input-number v-model="row.item_id" :min="1" />
      <span>数量</span>
      <el-input-number v-model="row.quantity" :min="1" />
      <span>expire_sec</span>
      <el-input-number v-model="row.expire_sec" :min="0" />
      <el-button size="small" @click="removeGiftRow(i)">删</el-button>
    </div>
    <el-button size="small" @click="addGiftRow">加赠送道具</el-button>
  </div>

  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>关键词</label>
      <el-input
        v-model="q"
        clearable
        class="admin-filter-control"
        placeholder="ID / 金额 / 钻石"
        @input="search"
      />
      <span class="admin-filter-tip">按档位 ID、金额分或钻石筛选</span>
    </div>
    <div class="admin-filter-field">
      <label>上架状态</label>
      <el-select v-model="enabledFilter" @change="search">
        <el-option value="all" label="全部" />
        <el-option value="on" label="已上架" />
        <el-option value="off" label="已下架" />
      </el-select>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="search">查询</el-button>
      <el-button @click="load">刷新</el-button>
    </div>
  </div>

  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="70" />
    <el-table-column prop="amount_fen" label="金额分" />
    <el-table-column prop="diamond" label="钻石" />
    <el-table-column prop="gift_diamond" label="赠钻" />
    <el-table-column label="赠道具" min-width="140">
      <template #default="{ row }">{{ formatGifts(row) }}</template>
    </el-table-column>
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
</template>
