<script setup lang="ts">
import { ElMessage } from 'element-plus'
import { api } from '../api'

async function download(kind: 'ledgers' | 'rounds' | 'claims') {
  try {
    const blob = await api.exportReport(kind, 5000)
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `${kind}.csv`
    a.click()
    URL.revokeObjectURL(url)
    ElMessage.success(`已下载 ${kind}.csv`)
  } catch (e) {
    ElMessage.error(String(e))
  }
}
</script>

<template>
  <h2>报表导出</h2>
  <p style="color: #64748b; margin-bottom: 16px">直连主库导出 CSV（默认最多 5000 行），操作写入审计。</p>
  <el-space>
    <el-button type="primary" @click="download('ledgers')">账变 ledger</el-button>
    <el-button type="primary" @click="download('rounds')">对局 game_round</el-button>
    <el-button type="primary" @click="download('claims')">活动领取 activity_claim</el-button>
  </el-space>
</template>
