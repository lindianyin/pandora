<script setup lang="ts">
import { computed } from 'vue'
import { RouterView, useRoute } from 'vue-router'

const route = useRoute()

const feltPaths = new Set([
  '/table',
  '/hzmj-table',
  '/phz-table',
  '/biji',
  '/hzmj-lab',
  '/ddz-lab',
  '/phz-lab',
  '/fish-lab',
  '/biji-lab',
  '/fish',
])

const felt = computed(() => feltPaths.has(route.path))
const wide = computed(() => felt.value)
</script>

<template>
  <div class="page" :class="{ wide, felt }">
    <header v-if="!felt">
      <h1>Pandora</h1>
      <p class="sub">登录 · 大厅匹配 · 牌桌</p>
    </header>
    <RouterView />
  </div>
</template>

<style>
:root {
  color-scheme: light;
  font-family: "Segoe UI", "PingFang SC", "Microsoft YaHei", system-ui, sans-serif;
  background: #eef2f6;
  color: #1a1a1a;
}
body { margin: 0; }
.page { max-width: 900px; margin: 0 auto; padding: 24px; }
.page.wide { max-width: 1560px; padding: 12px 16px; }
.page.felt { max-width: none; padding: 0; background: transparent; }
header h1 { margin: 0 0 8px; font-size: 1.5rem; }
.sub { margin: 0 0 20px; color: #666; }
</style>