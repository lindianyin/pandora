<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
const enabled = ref(false)
async function apply() {
  await ElMessageBox.confirm(`确认${enabled.value ? '开启' : '关闭'}维护模式？`)
  const r = await api.maintain(enabled.value)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'ok')
}
</script>
<template>
  <h2>运维 / 维护</h2>
  <el-switch v-model="enabled" active-text="维护中" inactive-text="正常" />
  <el-button style="margin-left: 12px" type="danger" @click="apply">应用</el-button>
</template>
