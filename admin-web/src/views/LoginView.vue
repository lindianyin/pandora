<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { api } from '../api'

const router = useRouter()
const username = ref('admin')
const password = ref('admin123')
const busy = ref(false)

async function login() {
  busy.value = true
  try {
    const r = await api.login(username.value, password.value)
    if (r.code !== 0) {
      ElMessage.error(r.message || '登录失败')
      return
    }
    localStorage.setItem('admin_token', r.data.access_token)
    localStorage.setItem('admin_user', r.data.username)
    localStorage.setItem('admin_role', r.data.role)
    router.push('/dashboard')
  } catch (e) {
    ElMessage.error(String(e))
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <div style="max-width: 360px; margin: 80px auto">
    <h2>运营后台登录</h2>
    <el-form @submit.prevent="login">
      <el-form-item label="账号"><el-input v-model="username" /></el-form-item>
      <el-form-item label="密码"><el-input v-model="password" type="password" show-password /></el-form-item>
      <el-button type="primary" :loading="busy" @click="login">登录</el-button>
    </el-form>
  </div>
</template>
