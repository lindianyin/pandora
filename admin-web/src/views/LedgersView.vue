<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

const items = ref<any[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(20)
const uid = ref<number>()

async function load() {
  const r = await api.ledgers({
    page: page.value,
    page_size: pageSize.value,
    uid: uid.value || undefined,
  })
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
  <h2>账变</h2>
  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>玩家 UID</label>
      <el-input-number v-model="uid" :min="0" controls-position="right" placeholder="0 表示不限" />
      <span class="admin-filter-tip">按玩家查货币账变；0/空为全部</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="search">查询</el-button>
      <el-button @click="uid = undefined; search()">清空条件</el-button>
    </div>
  </div>
  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="80" />
    <el-table-column prop="uid" label="UID" width="100" />
    <el-table-column prop="currency" label="币种" width="80" />
    <el-table-column prop="delta" label="变动" />
    <el-table-column prop="balance_after" label="余额" />
    <el-table-column prop="biz_type" label="业务" />
    <el-table-column prop="idempotent_key" label="幂等键" min-width="160" show-overflow-tooltip />
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
