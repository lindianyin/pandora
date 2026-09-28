<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

const items = ref<any[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(20)
const q = ref('')

async function load() {
  const r = await api.audit({ page: page.value, page_size: pageSize.value, q: q.value.trim() || undefined })
  if (r.code === 0) {
    items.value = r.data.items || []
    total.value = r.data.total || 0
  } else {
    ElMessage.error(r.message || '加载失败')
  }
}

function search() {
  page.value = 1
  load()
}

onMounted(load)
</script>
<template>
  <h2>审计</h2>
  <el-space style="margin-bottom: 12px">
    <el-input v-model="q" clearable placeholder="动作 / 目标" style="width: 220px" @keyup.enter="search" />
    <el-button type="primary" @click="search">查找</el-button>
    <el-button @click="load">刷新</el-button>
  </el-space>
  <el-table :data="items" border>
    <el-table-column prop="admin_id" label="管理员" width="90" />
    <el-table-column prop="action" label="动作" width="140" />
    <el-table-column prop="target" label="目标" min-width="120" show-overflow-tooltip />
    <el-table-column prop="before" label="前" min-width="100" show-overflow-tooltip />
    <el-table-column prop="after" label="后" min-width="100" show-overflow-tooltip />
    <el-table-column label="时间" width="170">
      <template #default="{ row }">{{ fmtTime(row.created_at) }}</template>
    </el-table-column>
  </el-table>
  <el-pagination
    style="margin-top: 12px; justify-content: flex-end"
    background
    layout="total, sizes, prev, pager, next"
    :total="total"
    v-model:current-page="page"
    v-model:page-size="pageSize"
    :page-sizes="[10, 20, 50]"
    @current-change="load"
    @size-change="
      () => {
        page = 1
        load()
      }
    "
  />
</template>
