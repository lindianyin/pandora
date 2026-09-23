<script setup lang="ts">
import { useGameSession } from '../composables/useGameSession'

const { busy, errorBanner, loginAndConnect, checkHealth, logs } = useGameSession()
</script>

<template>
  <section class="card">
    <h2>登录</h2>
    <p class="hint-text">游客登录后自动连接 WSS。三开请用多个窗口（各自独立 sessionStorage）。</p>
    <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>
    <div class="row">
      <button :disabled="busy" @click="loginAndConnect">游客登录并连接</button>
      <button @click="checkHealth">检查 /health</button>
    </div>
  </section>

  <section class="card">
    <h2>日志</h2>
    <pre class="log">{{ logs.join('\n') }}</pre>
  </section>
</template>

<style scoped>
.hint-text { color: #64748b; margin: 0 0 12px; font-size: 14px; }
.banner {
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 12px;
}
.banner.err { background: #fef2f2; color: #b91c1c; }
.row { display: flex; gap: 8px; flex-wrap: wrap; }
button {
  border: 0;
  background: #2563eb;
  color: #fff;
  padding: 8px 14px;
  border-radius: 6px;
  cursor: pointer;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
.card {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 16px;
  margin-bottom: 16px;
}
.log {
  margin: 0;
  max-height: 240px;
  overflow: auto;
  background: #0f172a;
  color: #e2e8f0;
  padding: 12px;
  border-radius: 8px;
  font-size: 12px;
}
</style>

