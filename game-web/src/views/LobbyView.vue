<script setup lang="ts">
import { onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useGameSession } from '../composables/useGameSession'

const router = useRouter()
const {
  uid,
  nickname,
  gold,
  diamond,
  wsOk,
  templates,
  matching,
  matchMsg,
  matchTimedOut,
  errorBanner,
  logs,
  refreshLobby,
  quickMatch,
  cancelMatch,
} = useGameSession()

onMounted(() => {
  if (!wsOk.value) {
    router.replace('/login')
    return
  }
  refreshLobby()
})
</script>

<template>
  <section class="card profile">
    <div>UID: {{ uid }} · {{ nickname }} · 金币 {{ gold }} / 钻石 {{ diamond }}</div>
    <div>WSS: {{ wsOk ? '已鉴权' : '未鉴权' }}</div>
    <div class="row">
      <button class="ghost" @click="router.push('/wallet')">钱包 / 充值</button>
      <button class="ghost" @click="router.push('/activity')">活动中心</button>
      <button class="ghost" @click="router.push('/record')">战绩</button>
      <button class="ghost" @click="router.push('/friends')">好友</button>
      <button class="ghost" @click="router.push('/mail')">邮件</button>
      <button class="ghost" @click="router.push('/rank')">排行</button>
      <button class="ghost" @click="router.push('/bag')">背包</button>
      <button class="ghost" @click="router.push('/hzmj-lab')">麻将四联调试</button>
      <button class="ghost" @click="router.push('/ddz-lab')">斗地主三联调试</button>
      <button class="ghost" @click="router.push('/phz-lab')">跑胡子三联调试</button>
    </div>
  </section>

  <div v-if="errorBanner" class="banner err">{{ errorBanner }}</div>

  <section class="card">
    <div class="head">
      <h2>大厅</h2>
      <button class="ghost" :disabled="matching" @click="refreshLobby">刷新场次</button>
    </div>

    <div v-if="matching" class="match-box">
      <span>匹配中… {{ matchMsg }}</span>
      <button @click="cancelMatch">取消匹配</button>
    </div>
    <div v-else-if="matchTimedOut" class="match-box warn">
      <span>{{ matchMsg || '匹配超时，请重试' }}</span>
    </div>

    <div v-if="!templates.length" class="empty">暂无场次，请点刷新或确认服务端已启动</div>
    <div v-for="t in templates" :key="t.id" class="tmpl">
      <div>
        <strong>{{ t.name }}</strong>
        <span class="meta">
          {{ t.game_id === 3000 || t.game_id === 2 ? '杭州麻将' : t.game_id === 4000 || t.game_id === 3 ? '跑胡子' : '斗地主' }} · {{ t.players || (t.game_id === 3000 || t.game_id === 2 ? 4 : 3) }}人
          · 底分 {{ t.base_score }} · 金币 [{{ t.min_gold }}, {{ t.max_gold }}]
        </span>
        <span v-if="!t.enabled" class="off">已关闭</span>
      </div>
      <button :disabled="matching || !t.enabled || !wsOk" @click="quickMatch(t.id)">快速匹配</button>
    </div>
  </section>

  <section class="card">
    <h2>日志</h2>
    <pre class="log">{{ logs.join('\n') }}</pre>
  </section>
</template>

<style scoped>
.card {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 16px;
  margin-bottom: 16px;
}
.profile { line-height: 1.6; font-size: 14px; }
.row { display: flex; gap: 10px; margin-top: 8px; }
.head { display: flex; justify-content: space-between; align-items: center; }
.head h2 { margin: 0; }
.tmpl {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 12px 0;
  border-bottom: 1px solid #eee;
}
.meta { display: block; color: #64748b; font-size: 13px; margin-top: 4px; }
.off { color: #b91c1c; font-size: 12px; margin-left: 8px; }
.empty { color: #94a3b8; padding: 12px 0; }
.match-box {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  background: #eff6ff;
  color: #1d4ed8;
  padding: 10px 12px;
  border-radius: 8px;
  margin-bottom: 12px;
}
.match-box.warn { background: #fff7ed; color: #c2410c; }
.banner {
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 12px;
}
.banner.err { background: #fef2f2; color: #b91c1c; }
button {
  border: 0;
  background: #2563eb;
  color: #fff;
  padding: 8px 14px;
  border-radius: 6px;
  cursor: pointer;
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.ghost {
  background: #e2e8f0;
  color: #334155;
}
.log {
  margin: 0;
  max-height: 240px;
  overflow: auto;
  background: #0f172a;
  color: #e2e8f0;
  padding: 12px;
  border-radius: 8px;
  font-size: 12px;
}
</style>
