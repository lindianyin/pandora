<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api'
const data = ref<any>({})
onMounted(async () => {
  const r = await api.dashboard()
  if (r.code === 0) data.value = r.data
})
</script>
<template>
  <h2>看板</h2>
  <el-descriptions :column="2" border>
    <el-descriptions-item label="CCU">{{ data.ccu }}</el-descriptions-item>
    <el-descriptions-item label="玩家数">{{ data.players }}</el-descriptions-item>
    <el-descriptions-item label="维护">{{ data.maintain }}</el-descriptions-item>
    <el-descriptions-item label="MySQL">{{ data.mysql }}</el-descriptions-item>
    <el-descriptions-item label="Redis">{{ data.redis }}</el-descriptions-item>
  </el-descriptions>
</template>
