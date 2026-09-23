<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'

const items = ref<any[]>([])
const form = ref({
  id: 0,
  type: 'sign',
  title: '',
  rules_json: '{"reward_key":"daily","reward":{"currency":1,"amount":500}}',
  enabled: true,
})

async function load() {
  const r = await api.activities()
  if (r.code === 0) items.value = r.data.items || []
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
  }
}

async function remove(id: number) {
  await ElMessageBox.confirm(`确认下架活动 ${id}?`, '二次确认')
  const r = await api.deleteActivity(id)
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
    <el-form-item label="启用"><el-switch v-model="form.enabled" /></el-form-item>
    <el-button type="primary" @click="save">保存 / 热更</el-button>
  </el-form>

  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="70" />
    <el-table-column prop="type" label="类型" width="90" />
    <el-table-column prop="title" label="标题" />
    <el-table-column label="rules" min-width="220">
      <template #default="{ row }">
        <code style="font-size: 12px">{{ typeof row.rules_json === 'string' ? row.rules_json : JSON.stringify(row.rules_json) }}</code>
      </template>
    </el-table-column>
    <el-table-column prop="enabled" label="启用" width="80" />
    <el-table-column prop="claim_count" label="领取次数" width="100" />
    <el-table-column label="操作" width="160">
      <template #default="{ row }">
        <el-button size="small" @click="edit(row)">编辑</el-button>
        <el-button size="small" type="danger" @click="remove(row.id)">下架</el-button>
      </template>
    </el-table-column>
  </el-table>
</template>
