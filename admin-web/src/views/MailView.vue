<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api } from '../api'
import { fmtTime } from '../utils'

type GiftItem = { item_id: number; quantity: number; expire_sec: number }

const title = ref('')
const body = ref('')
const scope = ref<'all' | 'uids'>('uids')
const uidsText = ref('')
const withCurrency = ref(false)
const withItems = ref(false)
const currency = ref(1)
const amount = ref(100)
const giftItems = ref<GiftItem[]>([{ item_id: 1001, quantity: 1, expire_sec: 0 }])
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
  const parts: string[] = []
  const amt = Number(a.amount || 0)
  if (amt > 0) {
    const cur = Number(a.currency || 1) === 2 ? '钻石' : '金币'
    parts.push(`${cur}${amt}`)
  }
  if (Array.isArray(a.items) && a.items.length) {
    parts.push(a.items.map((x: any) => `道具${x.item_id}x${x.quantity}`).join(','))
  }
  return parts.length ? parts.join(' · ') : '-'
}

function formatTarget(row: any): string {
  if (row.scope === 'all') return '全服'
  const uids = row.target_json?.uids
  if (Array.isArray(uids)) return uids.join(', ')
  return JSON.stringify(row.target_json || {})
}

function addGiftRow() {
  giftItems.value.push({ item_id: 1001, quantity: 1, expire_sec: 0 })
}

function removeGiftRow(i: number) {
  giftItems.value.splice(i, 1)
  if (!giftItems.value.length) giftItems.value.push({ item_id: 1001, quantity: 1, expire_sec: 0 })
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
  if (withCurrency.value && (!amount.value || amount.value <= 0)) {
    ElMessage.warning('货币数量须大于 0')
    return
  }
  const itemRows = withItems.value
    ? giftItems.value.filter((x) => x.item_id > 0 && x.quantity > 0)
    : []
  if (withItems.value && itemRows.length === 0) {
    ElMessage.warning('请填写至少一个有效道具')
    return
  }

  const attach: Record<string, unknown> = {}
  if (withCurrency.value) {
    attach.currency = currency.value
    attach.amount = amount.value
  }
  if (itemRows.length) attach.items = itemRows

  const scopeLabel = scope.value === 'all' ? '全服' : `指定 ${uids.length} 人`
  const attachLabel = formatAttach(attach)
  await ElMessageBox.confirm(`确认发送系统邮件？范围：${scopeLabel}${attachLabel !== '-' ? '，附件 ' + attachLabel : ''}`)

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
  <el-form label-width="96px" class="mail-form" style="max-width: 960px; margin-bottom: 24px">
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
    <el-form-item label="货币附件">
      <div class="attach-row">
        <el-switch v-model="withCurrency" active-text="带货币" inactive-text="无" />
        <template v-if="withCurrency">
          <el-select v-model="currency" class="field-currency">
            <el-option :value="1" label="金币" />
            <el-option :value="2" label="钻石" />
          </el-select>
          <el-input-number v-model="amount" :min="1" class="field-amount" controls-position="right" />
        </template>
      </div>
    </el-form-item>
    <el-form-item label="道具附件">
      <div class="items-attach">
        <el-switch v-model="withItems" active-text="带道具" inactive-text="无" />
        <div v-if="withItems" class="items-panel">
          <div class="items-head">
            <span class="col-id">道具 ID</span>
            <span class="col-qty">数量</span>
            <span class="col-exp">过期秒数</span>
            <span class="col-act">操作</span>
          </div>
          <div v-for="(row, i) in giftItems" :key="i" class="items-row">
            <el-input-number
              v-model="row.item_id"
              :min="1"
              class="col-id"
              controls-position="right"
            />
            <el-input-number
              v-model="row.quantity"
              :min="1"
              class="col-qty"
              controls-position="right"
            />
            <el-input-number
              v-model="row.expire_sec"
              :min="0"
              class="col-exp"
              controls-position="right"
            />
            <el-button class="col-act" size="small" @click="removeGiftRow(i)">删除</el-button>
          </div>
          <p class="items-hint">过期秒数填 0 表示用道具定义默认时长；ttl 类型为本次续期的单份秒数。</p>
          <el-button size="small" type="primary" plain @click="addGiftRow">添加一行道具</el-button>
        </div>
      </div>
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
    <el-table-column label="附件" min-width="180">
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

<style scoped>
.attach-row {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px;
  width: 100%;
}
.field-currency {
  width: 140px;
}
.field-amount {
  width: 160px;
}
.items-attach {
  width: 100%;
}
.items-panel {
  margin-top: 12px;
  padding: 12px 14px;
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  background: #f8fafc;
  width: 100%;
  box-sizing: border-box;
}
.items-head,
.items-row {
  display: grid;
  grid-template-columns: minmax(140px, 1.2fr) minmax(120px, 1fr) minmax(160px, 1.2fr) 72px;
  gap: 12px;
  align-items: center;
  margin-bottom: 10px;
}
.items-head {
  color: #64748b;
  font-size: 13px;
  margin-bottom: 8px;
}
.items-row :deep(.el-input-number) {
  width: 100%;
}
.items-hint {
  margin: 0 0 10px;
  color: #94a3b8;
  font-size: 12px;
  line-height: 1.5;
}
</style>
