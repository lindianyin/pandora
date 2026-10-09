<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { labTopupGold } from '../api'
import { GameSocket } from '../net/GameSocket'
import type { FishSnap, RoomSeat } from '../net/frame'

const router = useRouter()
const route = useRoute()
const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'

const gold = ref(0)
const mult = ref(1)
const mults = ref([1, 2, 5, 10])
const fishList = ref<FishSnap[]>([])
const logs = ref<string[]>([])
const roundId = ref(0)
const roomId = ref(Number(route.query.room_id) || 0)
const roomPhase = ref('')
const seats = ref<RoomSeat[]>([])
const myUid = ref(Number(sessionStorage.getItem('pandora_uid') || 0))
const mySeat = ref(-1)
const matching = ref(false)
const aimX = ref(960)
const aimY = ref(600)
let sock: GameSocket | null = null
let seq = 0
let anim: number | null = null

const seatSelf = computed(() => seats.value.find((s) => s.seat_id === mySeat.value) || null)
const iAmReady = computed(() => !!seatSelf.value?.ready)
const canMatch = computed(() => !roomId.value && !matching.value && !roundId.value)
const canReady = computed(() => !!roomId.value && !roundId.value && !iAmReady.value)
const canLeave = computed(() => !!roomId.value || !!roundId.value || matching.value)

function pushLog(line: string) {
  logs.value = [line, ...logs.value].slice(0, 30)
}

onMounted(() => {
  const token = sessionStorage.getItem('pandora_token')
  if (!token) {
    router.replace('/login')
    return
  }
  sock = new GameSocket({
    onLog: pushLog,
    onAuth: (ok, uid) => {
      if (uid) {
        myUid.value = uid
        sessionStorage.setItem('pandora_uid', String(uid))
      }
      if (ok && roomId.value && !roundId.value) {
        // already matched from lobby: one-click ready path remains manual/auto
      }
    },
    onMatchStatus: (s) => {
      matching.value = s.status === 0
      if (s.status === 1 && s.room_id) {
        roomId.value = s.room_id
        matching.value = false
        pushLog(`匹配成功 room=${s.room_id}`)
      }
    },
    onRoomState: (s) => {
      roomId.value = s.room_id
      roomPhase.value = s.phase
      seats.value = s.seats
      const me = s.seats.find((x) => x.uid === myUid.value)
      if (me) mySeat.value = me.seat_id
    },
    onFishGameStart: (g) => {
      roundId.value = g.round_id
      roomId.value = g.room_id || roomId.value
      mySeat.value = g.self_seat
      mults.value = g.cannon_mults.length ? g.cannon_mults : mults.value
      mult.value = mults.value[0] || 1
      fishList.value = g.fish
      const me = g.seats.find((s) => s.seat_id === g.self_seat)
      if (me) gold.value = me.gold
      pushLog(`开局 round=${g.round_id} room=${g.room_id}`)
    },
    onFishSpawn: (list) => {
      const map = new Map(fishList.value.map((f) => [f.fish_id, f]))
      for (const f of list) map.set(f.fish_id, f)
      fishList.value = [...map.values()]
    },
    onFishDespawn: (s) => {
      const dead = new Set(s.fish_ids)
      fishList.value = fishList.value.filter((f) => !dead.has(f.fish_id))
    },
    onFishFire: (f) => {
      if (!myUid.value || f.uid === myUid.value) gold.value = f.gold
    },
    onFishCatch: (c) => {
      fishList.value = fishList.value.filter((f) => f.fish_id !== c.fish_id)
      if (!myUid.value || c.uid === myUid.value) gold.value = c.gold
      pushLog(`捕获 +${c.reward}`)
    },
  })
  sock.connect(WSS_URL, token)
  anim = window.setInterval(() => {
    const dt = 0.05
    fishList.value = fishList.value.map((f) => ({
      ...f,
      x: f.x + f.vx * dt,
      y: f.y + f.vy * dt,
    }))
  }, 50)
})

onUnmounted(() => {
  if (anim != null) clearInterval(anim)
  sock?.close()
})

function oneClickMatch() {
  if (!canMatch.value) return
  matching.value = true
  sock?.quickMatch(4)
}

function oneClickReady() {
  if (!canReady.value) return
  sock?.ready(true)
}

async function topupGold() {
  const token = sessionStorage.getItem('pandora_token')
  if (!token) return
  try {
    const r = await labTopupGold(token, 100000)
    if (r.code !== 0) throw new Error(r.message || 'topup failed')
    gold.value = r.data.gold
    pushLog(`补给金币 +${r.data.added} → ${r.data.gold}`)
  } catch (e: unknown) {
    pushLog(String(e))
  }
}

function onPondClick(ev: MouseEvent) {
  if (!roundId.value) return
  const el = ev.currentTarget as HTMLElement
  const rect = el.getBoundingClientRect()
  aimX.value = ((ev.clientX - rect.left) / rect.width) * 1920
  aimY.value = (1 - (ev.clientY - rect.top) / rect.height) * 1080
  seq += 1
  sock?.fishFire(mult.value, aimX.value, aimY.value, seq)
}

function oneClickLeave() {
  if (matching.value) {
    sock?.cancelMatch()
    matching.value = false
  }
  if (roomId.value || roundId.value) {
    sock?.fishLeave()
    sock?.leaveRoom()
  }
  fishList.value = []
  roomId.value = 0
  roundId.value = 0
  roomPhase.value = ''
  seats.value = []
  mySeat.value = -1
  pushLog('已离开')
}

function backLobby() {
  oneClickLeave()
  router.push('/lobby')
}
</script>

<template>
  <div class="page">
    <header>
      <strong>捕鱼</strong>
      <span class="meta">房间 {{ roomId || '-' }}</span>
      <span class="meta">局号 {{ roundId || '-' }}</span>
      <span class="meta">相位 {{ roomPhase || '-' }}</span>
      <span>金币 {{ gold }}</span>
      <select v-model.number="mult" :disabled="!roundId" @change="sock?.fishSetMult(mult)">
        <option v-for="m in mults" :key="m" :value="m">{{ m }}倍</option>
      </select>
      <button :disabled="!canMatch" @click="oneClickMatch">一键匹配</button>
      <button :disabled="!canReady" @click="oneClickReady">一键准备</button>
      <button :disabled="!canLeave" @click="oneClickLeave">一键离开</button>
      <button @click="topupGold">补给金币</button>
      <button @click="backLobby">返回大厅</button>
    </header>
    <p v-if="matching" class="hint">匹配中…</p>
    <p v-else-if="iAmReady && !roundId" class="hint">已准备，等待开局</p>
    <p v-else-if="!roomId" class="hint">未入座：点「一键匹配」进捕鱼初级场</p>
    <p v-else-if="!roundId" class="hint">已入座：点「一键准备」开局</p>
    <div class="pond" @click="onPondClick">
      <div
        v-for="f in fishList"
        :key="f.fish_id"
        class="fish"
        :style="{ left: (f.x / 1920) * 100 + '%', bottom: (f.y / 1080) * 100 + '%' }"
      />
    </div>
    <pre class="log">{{ logs.join('\n') }}</pre>
  </div>
</template>

<style scoped>
.page { min-height: 100vh; background: #0a2230; color: #eef; padding: 12px; }
header { display: flex; gap: 12px; align-items: center; margin-bottom: 8px; flex-wrap: wrap; }
.meta { opacity: 0.9; font-variant-numeric: tabular-nums; }
.hint { margin: 0 0 8px; color: #9ad7c8; font-size: 13px; }
.pond {
  height: min(70vh, 540px);
  background: linear-gradient(180deg, #1c6b8f, #0c2c3a);
  border-radius: 8px;
  position: relative;
  overflow: hidden;
  cursor: crosshair;
}
.fish {
  position: absolute;
  width: 36px;
  height: 22px;
  background: #ffcc66;
  border-radius: 50%;
  transform: translate(-50%, 50%);
}
.log { margin-top: 8px; font-size: 12px; opacity: 0.8; max-height: 100px; overflow: auto; }
button { cursor: pointer; }
button:disabled { opacity: 0.4; cursor: not-allowed; }
</style>
