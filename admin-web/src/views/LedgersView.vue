<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api'
const items = ref<any[]>([])
onMounted(async () => {
  const r = await api.ledgers()
  if (r.code === 0) items.value = r.data.items || []
})
</script>
<template>
  <h2>账变</h2>
  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="80" />
    <el-table-column prop="uid" label="UID" width="100" />
    <el-table-column prop="currency" label="币种" width="80" />
    <el-table-column prop="delta" label="变动" />
    <el-table-column prop="balance_after" label="余额" />
    <el-table-column prop="biz_type" label="业务" />
    <el-table-column prop="idempotent_key" label="幂等键" />
  </el-table>
</template>
