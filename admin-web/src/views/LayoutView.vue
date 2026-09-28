<script setup lang="ts">
import { useRouter } from 'vue-router'
const router = useRouter()
const role = localStorage.getItem('admin_role') || ''
const user = localStorage.getItem('admin_user') || ''

const menus = [
  { path: '/dashboard', title: '看板' },
  { path: '/players', title: '玩家' },
  { path: '/ledgers', title: '账变' },
  { path: '/rounds', title: '对局' },
  { path: '/templates', title: '场次' },
  { path: '/pay/products', title: '充值档位' },
  { path: '/pay/orders', title: '订单' },
  { path: '/announce', title: '公告' },
  { path: '/mail', title: '邮件' },
  { path: '/ops', title: '运维' },
  { path: '/audit', title: '审计' },
  { path: '/activities', title: '活动' },
  { path: '/items', title: '道具' },
  { path: '/reports', title: '报表' },
]

function logout() {
  localStorage.removeItem('admin_token')
  router.push('/login')
}
</script>

<template>
  <el-container style="min-height: 100vh">
    <el-aside width="200px" style="background: #1f2937; color: #fff">
      <div style="padding: 16px; font-weight: 700">Pandora Admin</div>
      <el-menu background-color="#1f2937" text-color="#e5e7eb" active-text-color="#93c5fd" router :default-active="$route.path">
        <el-menu-item v-for="m in menus" :key="m.path" :index="m.path">{{ m.title }}</el-menu-item>
      </el-menu>
    </el-aside>
    <el-container>
      <el-header style="display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid #eee">
        <span>{{ user }} · {{ role }}</span>
        <el-button @click="logout">退出</el-button>
      </el-header>
      <el-main>
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

