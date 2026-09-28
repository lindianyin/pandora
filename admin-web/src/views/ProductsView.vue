<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'

const allItems = ref<any[]>([])
const q = ref('')
const enabledFilter = ref<'all' | 'on' | 'off'>('all')
const page = ref(1)
const pageSize = ref(20)
const form = ref({ id: 0, amount_fen: 600, diamond: 60, gift_diamond: 0, enabled: true })

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

async function load() {
  const r = await api.products()
  if (r.code === 0) allItems.value = r.data.items || []
  else ElMessage.error(r.message || '加载失败')
}

function search() {
  page.value = 1
}

async function save() {
  const r = await api.upsertProduct(form.value)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  if (r.code === 0) {
    form.value = { id: 0, amount_fen: 600, diamond: 60, gift_diamond: 0, enabled: true }
    load()
  }
}

function edit(row: any) {
  form.value = {
    id: row.id,
    amount_fen: row.amount_fen,
    diamond: row.diamond,
    gift_diamond: row.gift_diamond,
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
    <el-form-item label="赠"><el-input-number v-model="form.gift_diamond" /></el-form-item>
    <el-form-item label="上架"><el-switch v-model="form.enabled" /></el-form-item>
    <el-button type="primary" @click="save">保存</el-button>
  </el-form>

  <el-space style="margin-bottom: 12px">
    <el-input v-model="q" clearable placeholder="ID / 金额 / 钻石" style="width: 200px" @input="search" />
    <el-select v-model="enabledFilter" style="width: 120px" @change="search">
      <el-option value="all" label="全部" />
      <el-option value="on" label="已上架" />
      <el-option value="off" label="已下架" />
    </el-select>
    <el-button @click="load">刷新</el-button>
  </el-space>

  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="70" />
    <el-table-column prop="amount_fen" label="金额分" />
    <el-table-column prop="diamond" label="钻石" />
    <el-table-column prop="gift_diamond" label="赠送" />
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
