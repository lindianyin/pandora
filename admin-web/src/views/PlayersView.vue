<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

const items = ref<any[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(20)
const q = ref('')
const uid = ref<number>()
const delta = ref(100)
const currency = ref(1)

/** 与 schema 一致：0=正常 1=封禁 */
function statusText(s: number) {
  if (s === 1) return '封禁'
  if (s === 0) return '正常'
  return `未知(${s})`
}

function isBanned(row: { status?: number }) {
  return Number(row.status) === 1
}

async function load() {
  const r = await api.players({ page: page.value, page_size: pageSize.value, q: q.value.trim() || undefined })
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
  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>关键词</label>
      <el-input
        v-model="q"
        clearable
        class="admin-filter-control"
        placeholder="UID / 昵称 / open_id"
        @keyup.enter="search"
      />
      <span class="admin-filter-tip">支持 UID、昵称或 open_id 模糊查找</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="primary" @click="search">查询</el-button>
      <el-button @click="q = ''; search()">清空</el-button>
      <el-button @click="load">刷新</el-button>
    </div>
  </div>
  <div class="admin-filter">
    <div class="admin-filter-field">
      <label>补发 UID</label>
      <el-input-number v-model="uid" :min="1" controls-position="right" placeholder="玩家 UID" />
      <span class="admin-filter-tip">要补发或扣币的玩家</span>
    </div>
    <div class="admin-filter-field">
      <label>币种</label>
      <el-select v-model="currency">
        <el-option :value="1" label="金币" />
        <el-option :value="2" label="钻石" />
      </el-select>
    </div>
    <div class="admin-filter-field">
      <label>变动数量</label>
      <el-input-number v-model="delta" controls-position="right" />
      <span class="admin-filter-tip">正数补发，负数扣除</span>
    </div>
    <div class="admin-filter-actions">
      <el-button type="warning" @click="adjust">补发/扣除</el-button>
    </div>
  </div>
  <el-table :data="items" border>
    <el-table-column prop="uid" label="UID" width="100" />
    <el-table-column prop="nickname" label="昵称" />
    <el-table-column prop="gold" label="金币" />
    <el-table-column prop="diamond" label="钻石" />
    <el-table-column label="状态" width="100">
      <template #default="{ row }">
        <el-tag :type="isBanned(row) ? 'danger' : 'success'" size="small">
          {{ statusText(Number(row.status)) }}
        </el-tag>
      </template>
    </el-table-column>
    <el-table-column label="在线" width="90">
      <template #default="{ row }">
        <el-tag :type="row.online ? 'success' : 'info'" size="small" effect="plain">
          {{ row.online ? '在线' : '离线' }}
        </el-tag>
      </template>
    </el-table-column>
    <el-table-column label="注册时间" width="170">
      <template #default="{ row }">{{ fmtTime(row.created_at) }}</template>
    </el-table-column>
    <el-table-column label="操作" width="240">
      <template #default="{ row }">
        <el-button size="small" @click="kick(row.uid)">踢</el-button>
        <el-button v-if="!isBanned(row)" size="small" type="danger" @click="ban(row.uid, true)">封禁</el-button>
        <el-button v-else size="small" type="success" @click="ban(row.uid, false)">解封</el-button>
      </template>
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
