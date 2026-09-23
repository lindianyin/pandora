<script setup lang="ts">
import { RouterView } from 'vue-router'
</script>

<template>
  <div class="page">
    <header>
      <h1>Pandora · 斗地主 (M2b)</h1>
      <p class="sub">登录 · 大厅匹配 · 牌桌（SPEC 路由 /login /lobby /table）</p>
    </header>
    <RouterView />
  </div>
</template>

<style>
:root {
  color-scheme: light;
  font-family: "Segoe UI", system-ui, sans-serif;
  background: #eef2f6;
  color: #1a1a1a;
}
body { margin: 0; }
.page { max-width: 900px; margin: 0 auto; padding: 24px; }
header h1 { margin: 0 0 8px; font-size: 1.5rem; }
.sub { margin: 0 0 20px; color: #666; }
</style>

