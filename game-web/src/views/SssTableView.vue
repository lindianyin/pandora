<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { createSssLabClient } from '../composables/createSssLabClient'
import SssSeatPanel from '../components/SssSeatPanel.vue'

const router = useRouter()
const device = `sss-${Math.random().toString(16).slice(2, 10)}`
const client = createSssLabClient(0, device)

onMounted(() => client.login())
</script>

<template>
  <div class="page">
    <header class="bar">
      <div>
        <h1>十三水</h1>
        <p>头墩3张、中墩5张、尾墩5张；可选手动或智能摆牌。比牌与结算由服务端推送。</p>
      </div>
      <div class="nav">
        <button class="ghost" @click="router.push('/lobby')">大厅</button>
        <button class="ghost" @click="router.push('/sss-lab')">四联 Lab</button>
      </div>
    </header>
    <SssSeatPanel :client="client" :device-id="device" />
  </div>
</template>

<style scoped>
.page {
  min-height: 100vh;
  margin: -12px -16px;
  padding: 10px 12px 16px;
  background:
    radial-gradient(ellipse at top, rgba(40, 90, 70, 0.55), transparent 55%),
    linear-gradient(180deg, #10241c 0%, #0b1712 100%);
  color: #e8f2ec;
  max-width: 720px;
}
.bar {
  display: flex;
  justify-content: space-between;
  gap: 10px;
  flex-wrap: wrap;
  margin-bottom: 8px;
  align-items: center;
}
.bar h1 { margin: 0; font-size: 1.2rem; letter-spacing: 0.03em; }
.bar p { margin: 2px 0 0; opacity: 0.75; font-size: 12px; }
.nav { display: flex; gap: 6px; }
.ghost {
  border: 1px solid rgba(255, 255, 255, 0.22);
  background: transparent;
  color: #e8f2ec;
  border-radius: 6px;
  padding: 5px 10px;
  font-size: 12px;
  cursor: pointer;
}
</style>
