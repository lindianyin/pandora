<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api'

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
onMounted(async () => {
  const r = await api.rounds()
  if (r.code === 0) items.value = r.data.items || []
})
</script>
<template>
  <h2>对局</h2>
  <el-table :data="items" border>
    <el-table-column prop="round_id" label="局号" />
    <el-table-column prop="room_id" label="房间" />
    <el-table-column prop="template_id" label="场次" />
    <el-table-column prop="base_score" label="底分" />
    <el-table-column prop="multiplier" label="倍数" />
    <el-table-column label="玩家结算" min-width="220" show-overflow-tooltip>
      <template #default="{ row }">{{ formatPlayersSettle(row.players_json) }}</template>
    </el-table-column>
  </el-table>
</template>

