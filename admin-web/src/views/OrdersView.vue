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
const status = ref<number | undefined>(undefined)

const statusLabel: Record<number, string> = { 0: '待支付', 1: '成功', 2: '失败', 3: '关闭' }

async function load() {
  const r = await api.orders({
    page: page.value,
    page_size: pageSize.value,
    uid: uid.value || undefined,
    status: status.value === undefined || status.value < 0 ? undefined : status.value,
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
  <h2>支付订单</h2>
  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>玩家 UID</label>
      <el-input-number v-model="uid" :min="0" controls-position="right" placeholder="0 表示不限" />
      <span class="admin-filter-tip">留空或 0 表示不限玩家</span>
    </div>
    <div class="admin-filter-field">
      <label>订单状态</label>
      <el-select v-model="status" clearable placeholder="全部状态">
        <el-option :value="0" label="待支付" />
        <el-option :value="1" label="成功" />
        <el-option :value="2" label="失败" />
        <el-option :value="3" label="关闭" />
      </el-select>
      <span class="admin-filter-tip">可清空表示全部状态</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="search">查询</el-button>
      <el-button @click="uid = undefined; status = undefined; search()">清空条件</el-button>
    </div>
  </div>
  <el-table :data="items" border>
    <el-table-column prop="order_id" label="订单号" min-width="160" />
    <el-table-column prop="uid" label="UID" width="100" />
    <el-table-column prop="product_id" label="档位" width="80" />
    <el-table-column prop="amount_fen" label="金额分" width="100" />
    <el-table-column prop="diamond" label="钻石" width="80" />
    <el-table-column label="状态" width="90">
      <template #default="{ row }">{{ statusLabel[row.status] ?? row.status }}</template>
    </el-table-column>
    <el-table-column label="创建时间" width="170">
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
