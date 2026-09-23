<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
const items = ref<any[]>([])
const uid = ref<number>()
const delta = ref(100)
const currency = ref(1)

async function load() {
  const r = await api.players()
  if (r.code === 0) items.value = r.data.items || []
}
async function kick(u: number) {
  await ElMessageBox.confirm(`踢下线 UID ${u}?`)
  const r = await api.kick(u)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || (r.code === 0 ? 'ok' : 'fail'))
}
async function ban(u: number, on: boolean) {
  await ElMessageBox.confirm(`${on ? '封禁' : '解封'} UID ${u}?`)
  const r = on ? await api.ban(u) : await api.unban(u)
  ElMessage[r.code === 0 ? 'success' : 'error'](r.message || 'done')
  load()
}
async function adjust() {
  if (!uid.value) return
  await ElMessageBox.confirm(`补发/扣币 UID ${uid.value} delta=${delta.value}?`)
  const r = await api.adjust({
    uid: uid.value,
    currency: currency.value,
    delta: delta.value,
    idempotent_key: `web-${Date.now()}`,
  })
  ElMessage[r.code === 0 ? 'success' : 'error'](r.code === 0 ? `balance=${r.data.balance}` : r.message)
  load()
}
onMounted(load)
</script>
<template>
  <h2>玩家</h2>
  <el-space style="margin-bottom: 12px">
    <el-input-number v-model="uid" placeholder="UID" />
    <el-select v-model="currency" style="width: 120px">
      <el-option :value="1" label="金币" />
      <el-option :value="2" label="钻石" />
    </el-select>
    <el-input-number v-model="delta" />
    <el-button type="warning" @click="adjust">补发/扣除</el-button>
    <el-button @click="load">刷新</el-button>
  </el-space>
  <el-table :data="items" border>
    <el-table-column prop="uid" label="UID" width="100" />
    <el-table-column prop="nickname" label="昵称" />
    <el-table-column prop="gold" label="金币" />
    <el-table-column prop="diamond" label="钻石" />
    <el-table-column prop="status" label="状态" width="80" />
    <el-table-column prop="online" label="在线" width="80" />
    <el-table-column label="操作" width="240">
      <template #default="{ row }">
        <el-button size="small" @click="kick(row.uid)">踢</el-button>
        <el-button size="small" type="danger" @click="ban(row.uid, true)">封</el-button>
        <el-button size="small" @click="ban(row.uid, false)">解</el-button>
      </template>
    </el-table-column>
  </el-table>
</template>
