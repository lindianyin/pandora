<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { createBijiLabClient } from '../composables/createBijiLabClient'
import BijiSeatPanel from '../components/BijiSeatPanel.vue'

const router = useRouter()
const device = `biji-${Math.random().toString(16).slice(2, 10)}`
const client = createBijiLabClient(0, device)

onMounted(() => client.login())
</script>

<template>
  <div class="page">
    <header class="bar">
      <div>
        <h1>比鸡</h1>
        <p>点选手牌，再点头/中/尾墩放入；点墩内牌可取回。须头≤中≤尾后确认。</p>
      </div>
      <div class="nav">
        <button class="ghost" @click="router.push('/lobby')">大厅</button>
        <button class="ghost" @click="router.push('/biji-lab')">四联 Lab</button>
      </div>
    </header>
    <BijiSeatPanel :client="client" :device-id="device" />
  </div>
</template>

<style scoped>
.page {
  min-height: 100vh;
  margin: -12px -16px;
  padding: 16px 18px 28px;
  background:
    radial-gradient(ellipse at top, rgba(40, 90, 70, 0.55), transparent 55%),
    linear-gradient(180deg, #10241c 0%, #0b1712 100%);
  color: #e8f2ec;
  max-width: 760px;
}
.bar {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
  margin-bottom: 12px;
}
.bar h1 { margin: 0; font-size: 1.5rem; letter-spacing: 0.04em; }
.bar p { margin: 4px 0 0; opacity: 0.8; font-size: 13px; }
.nav { display: flex; gap: 8px; }
.ghost {
  border: 1px solid rgba(255, 255, 255, 0.22);
  background: transparent;
  color: #e8f2ec;
  border-radius: 8px;
  padding: 7px 12px;
  cursor: pointer;
}
</style>