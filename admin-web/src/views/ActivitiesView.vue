<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

const allItems = ref<any[]>([])
const q = ref('')
const page = ref(1)
const pageSize = ref(20)
const form = ref({
  id: 0,
  type: 'sign',
  title: '',
  rules_json: '{"reward_key":"daily","reward":{"currency":1,"amount":500}}',
  enabled: true,
  start_at: '',
  end_at: '',
})

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

async function save() {
  let rules: unknown = {}
  try {
    rules = JSON.parse(form.value.rules_json || '{}')
  } catch {
    ElMessage.error('rules_json 不是合法 JSON')
    return
  }
  const r = await api.upsertActivity({
    id: form.value.id,
    type: form.value.type,
    title: form.value.title,
    rules_json: rules,
    enabled: form.value.enabled,
    start_at: form.value.start_at || undefined,
    end_at: form.value.end_at || undefined,
  })
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  if (r.code === 0) {
    form.value.id = 0
    load()
  }
}

function edit(row: any) {
  form.value = {
    id: row.id,
    type: row.type,
    title: row.title,
    rules_json: typeof row.rules_json === 'string' ? row.rules_json : JSON.stringify(row.rules_json || {}),
    enabled: !!row.enabled,
    start_at: row.start_at || '',
    end_at: row.end_at || '',
  }
}

async function setEnabled(row: any, enabled: boolean) {
  const label = enabled ? '上架' : '下架'
  await ElMessageBox.confirm(`确认${label}活动 ${row.id}「${row.title}」?`, '二次确认')
  const r = enabled ? await api.setActivityEnabled(row.id, true) : await api.deleteActivity(row.id)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  load()
}

onMounted(load)
</script>

<template>
  <h2>活动管理</h2>
  <el-form :model="form" label-width="100px" style="max-width: 720px; margin-bottom: 16px">
    <el-form-item label="ID(0=新建)"><el-input-number v-model="form.id" /></el-form-item>
    <el-form-item label="类型">
      <el-select v-model="form.type" style="width: 160px">
        <el-option label="sign 签到" value="sign" />
        <el-option label="task 任务" value="task" />
        <el-option label="gift 礼包" value="gift" />
      </el-select>
    </el-form-item>
    <el-form-item label="标题"><el-input v-model="form.title" /></el-form-item>
    <el-form-item label="rules_json"><el-input v-model="form.rules_json" type="textarea" :rows="4" /></el-form-item>
    <el-form-item label="开始时间">
      <el-input v-model="form.start_at" placeholder="YYYY-MM-DD HH:mm:ss，空=不限" />
    </el-form-item>
    <el-form-item label="结束时间">
      <el-input v-model="form.end_at" placeholder="YYYY-MM-DD HH:mm:ss，空=不限" />
    </el-form-item>
    <el-form-item label="上架"><el-switch v-model="form.enabled" /></el-form-item>
    <el-button type="primary" @click="save">保存 / 热更</el-button>
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
    <el-table-column label="rules" min-width="180">
      <template #default="{ row }">
        <code style="font-size: 12px">{{ typeof row.rules_json === 'string' ? row.rules_json : JSON.stringify(row.rules_json) }}</code>
      </template>
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
