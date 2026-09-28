<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

type PlayerSettle = {
  uid?: number
  seat_id?: number
  delta?: number
  delta_gold?: number
  nickname?: string
}

function formatPlayersSettle(raw: unknown): string {
  if (raw == null || raw === '') return '-'
  let list: PlayerSettle[]
  if (typeof raw === 'string') {
    try {
      list = JSON.parse(raw) as PlayerSettle[]
    } catch {
      return raw
    }
  } else if (Array.isArray(raw)) {
    list = raw as PlayerSettle[]
  } else {
    return String(raw)
  }
  if (!list.length) return '-'
  return list
    .map((p) => {
      const label = p.nickname?.trim() || (p.uid != null ? `UID ${p.uid}` : '?')
      const score = p.delta ?? p.delta_gold
      if (score == null) return label
      const signed = score > 0 ? `+${score}` : String(score)
      return `${label}:${signed}`
    })
    .join('、')
}

const items = ref<any[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(20)
const uid = ref<number>()

async function load() {
  const r = await api.rounds({
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
  <h2>对局</h2>
  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>玩家 UID</label>
      <el-input-number v-model="uid" :min="0" controls-position="right" placeholder="0 表示不限" />
      <span class="admin-filter-tip">按玩家过滤对局；0/空为全部</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="search">查询</el-button>
      <el-button @click="uid = undefined; search()">清空条件</el-button>
    </div>
  </div>
  <el-table :data="items" border>
    <el-table-column prop="round_id" label="局号" width="120" />
    <el-table-column prop="room_id" label="房间" width="120" />
    <el-table-column prop="template_id" label="场次" width="80" />
    <el-table-column prop="base_score" label="底分" width="80" />
    <el-table-column prop="multiplier" label="倍数" width="80" />
    <el-table-column label="玩家结算" min-width="220" show-overflow-tooltip>
      <template #default="{ row }">{{ formatPlayersSettle(row.players_json) }}</template>
    </el-table-column>
    <el-table-column label="开始时间" width="170">
      <template #default="{ row }">{{ fmtTime(row.started_at) }}</template>
    </el-table-column>
    <el-table-column label="结束时间" width="170">
      <template #default="{ row }">{{ fmtTime(row.ended_at) }}</template>
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
