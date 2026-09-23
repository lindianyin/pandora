<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api'
const items = ref<any[]>([])
onMounted(async () => {
  const r = await api.rounds()
  if (r.code === 0) items.value = r.data.items || []
})
</script>
<template>
  <h2>对局</h2>
  <el-table :data="items" border>
    <el-table-column prop="round_id" label="局号" />
    <el-table-column prop="room_id" label="房间" />
    <el-table-column prop="template_id" label="场次" />
    <el-table-column prop="base_score" label="底分" />
    <el-table-column prop="multiplier" label="倍数" />
    <el-table-column prop="players_json" label="玩家结算" />
  </el-table>
</template>
