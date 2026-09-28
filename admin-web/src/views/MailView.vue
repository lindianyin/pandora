<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

const title = ref('')
const body = ref('')
const scope = ref<'all' | 'uids'>('uids')
const uidsText = ref('')
const withAttach = ref(false)
const currency = ref(1)
const amount = ref(100)
const sending = ref(false)
const items = ref<any[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(20)

function parseUids(text: string): number[] {
  const out: number[] = []
  const seen = new Set<number>()
  for (const part of text.split(/[\s,;，；]+/)) {
    const s = part.trim()
    if (!s) continue
    const n = Number(s)
    if (!Number.isInteger(n) || n <= 0 || seen.has(n)) continue
    seen.add(n)
    out.push(n)
  }
  return out
}

function formatAttach(a: any): string {
  if (!a || typeof a !== 'object') return '-'
  const amt = Number(a.amount || 0)
  if (!amt) return '-'
  const cur = Number(a.currency || 1) === 2 ? '钻石' : '金币'
  return `${cur} ${amt}`
}

function formatTarget(row: any): string {
  if (row.scope === 'all') return '全服'
  const uids = row.target_json?.uids
  if (Array.isArray(uids)) return uids.join(', ')
  return JSON.stringify(row.target_json || {})
}

async function load() {
  const r = await api.mailLogs({ page: page.value, page_size: pageSize.value })
  if (r.code === 0) {
    items.value = r.data.items || []
    total.value = r.data.total || 0
  } else {
    ElMessage.error(r.message || '加载失败')
  }
}

async function send() {
  if (!title.value.trim()) {
    ElMessage.warning('请填写标题')
    return
  }
  const uids = scope.value === 'uids' ? parseUids(uidsText.value) : []
  if (scope.value === 'uids' && uids.length === 0) {
    ElMessage.warning('请填写至少一个 UID')
    return
  }
  if (withAttach.value && (!amount.value || amount.value <= 0)) {
    ElMessage.warning('附件数量须大于 0')
    return
  }

  const attach = withAttach.value ? { currency: currency.value, amount: amount.value } : {}
  const scopeLabel = scope.value === 'all' ? '全服' : `指定 ${uids.length} 人`
  const attachLabel = withAttach.value
    ? `，附件 ${currency.value === 2 ? '钻石' : '金币'} ${amount.value}`
    : ''
  await ElMessageBox.confirm(`确认发送系统邮件？范围：${scopeLabel}${attachLabel}`)

  sending.value = true
  try {
    const r = await api.sendMail({
      scope: scope.value,
      uids: scope.value === 'uids' ? uids : undefined,
      title: title.value.trim(),
      body: body.value,
      attach_json: attach,
    })
    ElMessage[r.code === 0 ? 'success' : 'error'](r.message || (r.code === 0 ? '已发送' : '发送失败'))
    if (r.code === 0) {
      title.value = ''
      body.value = ''
      page.value = 1
      load()
    }
  } finally {
    sending.value = false
  }
}

onMounted(load)
</script>

<template>
  <h2>系统邮件</h2>
  <el-form label-width="96px" style="max-width: 640px; margin-bottom: 24px">
    <el-form-item label="标题">
      <el-input v-model="title" maxlength="128" show-word-limit placeholder="邮件标题" />
    </el-form-item>
    <el-form-item label="正文">
      <el-input v-model="body" type="textarea" :rows="4" maxlength="1024" show-word-limit />
    </el-form-item>
    <el-form-item label="收件范围">
      <el-radio-group v-model="scope">
        <el-radio value="uids">指定 UID</el-radio>
        <el-radio value="all">全服</el-radio>
      </el-radio-group>
    </el-form-item>
    <el-form-item v-if="scope === 'uids'" label="UID 列表">
      <el-input
        v-model="uidsText"
        type="textarea"
        :rows="3"
        placeholder="多个 UID 用逗号、空格或换行分隔"
      />
    </el-form-item>
    <el-form-item label="附件">
      <el-switch v-model="withAttach" active-text="带奖励" inactive-text="无" />
      <template v-if="withAttach">
        <el-select v-model="currency" style="width: 120px; margin-left: 12px">
          <el-option :value="1" label="金币" />
          <el-option :value="2" label="钻石" />
        </el-select>
        <el-input-number v-model="amount" :min="1" style="margin-left: 12px" />
      </template>
    </el-form-item>
    <el-form-item>
      <el-button type="primary" :loading="sending" @click="send">发送</el-button>
      <el-button @click="load">刷新记录</el-button>
    </el-form-item>
  </el-form>

  <h3>发送记录（共 {{ total }}）</h3>
  <el-table :data="items" border>
    <el-table-column prop="id" label="ID" width="80" />
    <el-table-column prop="admin_id" label="Admin" width="80" />
    <el-table-column prop="scope" label="范围" width="80" />
    <el-table-column label="目标" min-width="160" show-overflow-tooltip>
      <template #default="{ row }">{{ formatTarget(row) }}</template>
    </el-table-column>
    <el-table-column prop="title" label="标题" min-width="140" show-overflow-tooltip />
    <el-table-column label="附件" width="120">
      <template #default="{ row }">{{ formatAttach(row.attach_json) }}</template>
    </el-table-column>
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
