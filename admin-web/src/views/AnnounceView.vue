<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '../api'
const message = ref('全服公告测试')
async function send() {
  const r = await api.announce(message.value)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'sent')
}
</script>
<template>
  <h2>公告</h2>
  <el-input v-model="message" type="textarea" rows="4" />
  <el-button style="margin-top: 12px" type="primary" @click="send">发布</el-button>
</template>
