<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
const items = ref<any[]>([])
const form = ref({ id: 0, amount_fen: 600, diamond: 60, gift_diamond: 0, enabled: true })
async function load() {
  const r = await api.products()
  if (r.code === 0) items.value = r.data.items || []
}
async function save() {
  const r = await api.upsertProduct(form.value)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
  load()
}
async function remove(id: number) {
  await ElMessageBox.confirm(`下架档位 ${id}?`)
  await api.deleteProduct(id)
  load()
}
onMounted(load)
</script>
<template>
  <h2>充值档位</h2>
  <el-form :inline="true" :model="form">
    <el-form-item label="ID(0=新建)"><el-input-number v-model="form.id" /></el-form-item>
    <el-form-item label="分"><el-input-number v-model="form.amount_fen" /></el-form-item>
    <el-form-item label="钻"><el-input-number v-model="form.diamond" /></el-form-item>
    <el-form-item label="赠"><el-input-number v-model="form.gift_diamond" /></el-form-item>
    <el-button type="primary" @click="save">保存</el-button>
  </el-form>
  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="70" />
    <el-table-column prop="amount_fen" label="金额分" />
    <el-table-column prop="diamond" label="钻石" />
    <el-table-column prop="gift_diamond" label="赠送" />
    <el-table-column prop="enabled" label="启用" />
    <el-table-column label="操作" width="100">
      <template #default="{ row }"><el-button size="small" @click="remove(row.id)">下架</el-button></template>
    </el-table-column>
  </el-table>
</template>
