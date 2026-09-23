<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api'
const items = ref<any[]>([])
onMounted(async () => {
  const r = await api.audit()
  if (r.code === 0) items.value = r.data.items || []
})
</script>
<template>
  <h2>审计</h2>
  <el-table :data="items" border>
    <el-table-column prop="admin_id" label="管理员" width="90" />
    <el-table-column prop="action" label="动作" />
    <el-table-column prop="target" label="目标" />
    <el-table-column prop="before" label="前" />
    <el-table-column prop="after" label="后" />
    <el-table-column prop="created_at" label="时间" />
  </el-table>
</template>
