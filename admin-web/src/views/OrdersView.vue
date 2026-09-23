<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api'
const items = ref<any[]>([])
onMounted(async () => {
  const r = await api.orders()
  if (r.code === 0) items.value = r.data.items || []
})
</script>
<template>
  <h2>支付订单</h2>
  <el-table :data="items" border>
    <el-table-column prop="order_id" label="订单号" />
    <el-table-column prop="uid" label="UID" />
    <el-table-column prop="product_id" label="档位" />
    <el-table-column prop="amount_fen" label="金额分" />
    <el-table-column prop="diamond" label="钻石" />
    <el-table-column prop="status" label="状态" />
  </el-table>
</template>
