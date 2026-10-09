<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '../api'

const items = ref<any[]>([])
const form = ref({
  id: 1,
  game_id: 2000,
  name: '初级场',
  base_score: 100,
  rake_bp: 500,
  min_gold: 1000,
  max_gold: 0,
  enabled: true,
})

function gameLabel(gid: number) {
  if (gid === 3000 || gid === 2) return '杭州麻将'
  if (gid === 4000 || gid === 3) return '跑胡子'
  return '斗地主'
}

async function load() {
  const r = await api.templates()
  if (r.code === 0) items.value = r.data.items || []
}

function editRow(row: any) {
  form.value = {
    id: row.id,
    game_id: row.game_id ?? 2000,
    name: row.name,
    base_score: row.base_score,
    rake_bp: row.rake_bp,
    min_gold: row.min_gold,
    max_gold: row.max_gold ?? 0,
    enabled: !!row.enabled,
  }
}

async function save() {
  const r = await api.putTemplate(form.value)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'saved')
  load()
}

onMounted(load)
</script>
<template>
  <h2>场次热更</h2>
  <el-form :inline="true" :model="form" style="margin-bottom: 12px">
    <el-form-item label="ID"><el-input-number v-model="form.id" /></el-form-item>
    <el-form-item label="game_id"><el-input-number v-model="form.game_id" :step="1000" /></el-form-item>
    <el-form-item label="名称"><el-input v-model="form.name" /></el-form-item>
    <el-form-item label="底分"><el-input-number v-model="form.base_score" /></el-form-item>
    <el-form-item label="抽水bp"><el-input-number v-model="form.rake_bp" /></el-form-item>
    <el-form-item label="最低金"><el-input-number v-model="form.min_gold" /></el-form-item>
    <el-form-item label="启用"><el-switch v-model="form.enabled" /></el-form-item>
    <el-button type="primary" @click="save">保存并热更</el-button>
  </el-form>
  <el-table :data="items" border @row-click="editRow">
    <el-table-column prop="id" label="ID" width="70" />
    <el-table-column prop="game_id" label="game_id" width="100" />
    <el-table-column label="玩法" width="100">
      <template #default="{ row }">{{ gameLabel(row.game_id) }}</template>
    </el-table-column>
    <el-table-column prop="players" label="人数" width="70" />
    <el-table-column prop="name" label="名称" />
    <el-table-column prop="base_score" label="底分" />
    <el-table-column prop="rake_bp" label="抽水" />
    <el-table-column prop="min_gold" label="最低" />
    <el-table-column prop="enabled" label="启用" />
  </el-table>
</template>
