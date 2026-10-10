<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import FishCannon from '../components/FishCannon.vue'
import { labTopupGold } from '../api'
import { GameSocket } from '../net/GameSocket'
import {
  FISH_SEAT_CANNON_POS,
  fishCannonAngle,
  fishCannonAngleFromVel,
  fishSeatIsTop,
  fishVisual,
  type FishSeatInfo,
  type FishSnap,
  type RoomSeat,
} from '../net/frame'

const router = useRouter()
const route = useRoute()
const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'

const gold = ref(0)
const mult = ref(1)
const mults = ref([1, 2, 5, 10])
const fishList = ref<FishSnap[]>([])
const fishSeats = ref<FishSeatInfo[]>([])
const cannonAngles = ref<number[]>([0, 0, 180, 180])
const firingSeat = ref(-1)
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
const lastReward = ref(0)
let sock: GameSocket | null = null
/** Last used fire client_seq; resume from server last_client_seq after reconnect. */
let seq = 0
let anim: number | null = null
let fireFlashTimer: number | null = null

function noteFireSeq(v: number) {
  if (v > seq) seq = v
}

function nextFireSeq() {
  seq += 1
  return seq
}

const seatSelf = computed(() => seats.value.find((s) => s.seat_id === mySeat.value) || null)
const iAmReady = computed(() => !!seatSelf.value?.ready)
const canMatch = computed(() => !roomId.value && !matching.value && !roundId.value)
const canReady = computed(() => !!roomId.value && !roundId.value && !iAmReady.value)
const canLeave = computed(() => !!roomId.value || !!roundId.value || matching.value)
const statusText = computed(() => {
  if (matching.value) return '匹配中…'
  if (iAmReady.value && !roundId.value) return '已准备，等待开局'
  if (!roomId.value) return '点「一键匹配」进入捕鱼场'
  if (!roundId.value) return '已入座，点「一键准备」开局'
  return '移动瞄准，点击渔场开火'
})

const cannons = computed(() =>
  [0, 1, 2, 3].map((seat) => {
    const pos = FISH_SEAT_CANNON_POS[seat]!
    const info = fishSeats.value.find((s) => s.seat_id === seat)
    const isMe = seat === mySeat.value
    return {
      seat,
      leftPct: (pos.x / 1920) * 100,
      bottomPct: (pos.y / 1080) * 100,
      angle: cannonAngles.value[seat] ?? (fishSeatIsTop(seat) ? 180 : 0),
      mult: isMe ? mult.value : info?.cannon_mult || 1,
      nickname: info?.nickname || `座${seat}`,
      me: isMe,
      empty: !info || !info.uid,
      firing: firingSeat.value === seat,
      top: fishSeatIsTop(seat),
    }
  }),
)

function pushLog(line: string) {
  logs.value = [line, ...logs.value].slice(0, 30)
}

function face(f: FishSnap) {
  return fishVisual(f.type_id, f.radius)
}

function setFishSeat(s: FishSeatInfo) {
  const rest = fishSeats.value.filter((x) => x.seat_id !== s.seat_id)
  fishSeats.value = [...rest, s].sort((a, b) => a.seat_id - b.seat_id)
}

function aimSeat(seat: number, ax: number, ay: number) {
  const pos = FISH_SEAT_CANNON_POS[seat]
  if (!pos) return
  const next = cannonAngles.value.slice()
  next[seat] = fishCannonAngle(pos.x, pos.y, ax, ay)
  cannonAngles.value = next
}

function flashFire(seat: number) {
  firingSeat.value = seat
  if (fireFlashTimer != null) clearTimeout(fireFlashTimer)
  fireFlashTimer = window.setTimeout(() => {
    if (firingSeat.value === seat) firingSeat.value = -1
  }, 140)
}

function pondPoint(ev: MouseEvent) {
  const el = ev.currentTarget as HTMLElement
  const rect = el.getBoundingClientRect()
  return {
    x: ((ev.clientX - rect.left) / rect.width) * 1920,
    y: (1 - (ev.clientY - rect.top) / rect.height) * 1080,
  }
}

onMounted(() => {
  const token = sessionStorage.getItem('pandora_token')
  if (!token) {
    router.replace('/login')
    return
  }
  sock = new GameSocket({
    onLog: pushLog,
    onAuth: (_ok, uid) => {
      if (uid) {
        myUid.value = uid
        sessionStorage.setItem('pandora_uid', String(uid))
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
      fishSeats.value = [...g.seats]
      const me = g.seats.find((s) => s.seat_id === g.self_seat)
      if (me) {
        gold.value = me.gold
        mult.value = me.cannon_mult || mult.value
        noteFireSeq(me.last_client_seq || 0)
      }
      pushLog(`开局 round=${g.round_id} room=${g.room_id} fireSeq=${seq}`)
    },
    onFishSeatUpdate: (s) => {
      setFishSeat(s)
      if (s.seat_id === mySeat.value) {
        gold.value = s.gold
        if (s.cannon_mult > 0) mult.value = s.cannon_mult
        noteFireSeq(s.last_client_seq || 0)
      }
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
      if (!myUid.value || f.uid === myUid.value) {
        gold.value = f.gold
        noteFireSeq(f.client_seq || 0)
      }
      const next = cannonAngles.value.slice()
      next[f.seat_id] = fishCannonAngleFromVel(f.vx, f.vy)
      cannonAngles.value = next
      flashFire(f.seat_id)
      const info = fishSeats.value.find((s) => s.seat_id === f.seat_id)
      if (info) {
        setFishSeat({
          ...info,
          cannon_mult: f.mult,
          gold: f.gold,
          last_client_seq: Math.max(info.last_client_seq || 0, f.client_seq || 0),
        })
      }
    },
    onFishCatch: (c) => {
      fishList.value = fishList.value.filter((f) => f.fish_id !== c.fish_id)
      if (!myUid.value || c.uid === myUid.value) {
        gold.value = c.gold
        lastReward.value = c.reward
      }
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
  if (fireFlashTimer != null) clearTimeout(fireFlashTimer)
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

function onPondMove(ev: MouseEvent) {
  if (!roundId.value || mySeat.value < 0) return
  const p = pondPoint(ev)
  aimX.value = p.x
  aimY.value = p.y
  aimSeat(mySeat.value, p.x, p.y)
}

function onPondClick(ev: MouseEvent) {
  if (!roundId.value) return
  const p = pondPoint(ev)
  aimX.value = p.x
  aimY.value = p.y
  if (mySeat.value >= 0) aimSeat(mySeat.value, p.x, p.y)
  sock?.fishFire(mult.value, aimX.value, aimY.value, nextFireSeq())
}

function onMultChange() {
  sock?.fishSetMult(mult.value)
  if (mySeat.value >= 0) {
    const info = fishSeats.value.find((s) => s.seat_id === mySeat.value)
    if (info) setFishSeat({ ...info, cannon_mult: mult.value })
  }
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
  fishSeats.value = []
  cannonAngles.value = [0, 0, 180, 180]
  roomId.value = 0
  roundId.value = 0
  roomPhase.value = ''
  seats.value = []
  mySeat.value = -1
  lastReward.value = 0
  seq = 0
  pushLog('已离开')
}

function backLobby() {
  oneClickLeave()
  router.push('/lobby')
}
</script>

<template>
  <div class="ocean">
    <header class="topbar">
      <div>
        <h1>捕鱼</h1>
        <p>
          房间 {{ roomId || '-' }} · 局 {{ roundId || '-' }} · {{ roomPhase || 'idle' }}
          <span v-if="mySeat >= 0"> · 座 {{ mySeat }}</span>
        </p>
      </div>
      <div class="hud">
        <div class="gold-pill">
          <span>金币</span>
          <strong>{{ gold }}</strong>
          <em v-if="lastReward > 0">+{{ lastReward }}</em>
        </div>
        <label class="mult">
          炮倍
          <select v-model.number="mult" :disabled="!roundId" @change="onMultChange">
            <option v-for="m in mults" :key="m" :value="m">{{ m }}×</option>
          </select>
        </label>
        <button class="primary" :disabled="!canMatch" @click="oneClickMatch">一键匹配</button>
        <button class="primary" :disabled="!canReady" @click="oneClickReady">一键准备</button>
        <button class="ghost" :disabled="!canLeave" @click="oneClickLeave">全部离开</button>
        <button class="ghost" @click="topupGold">补给金币</button>
        <button class="ghost" @click="backLobby">大厅</button>
        <button class="ghost" @click="router.push('/fish-lab')">双联 Lab</button>
      </div>
    </header>

    <p class="status">{{ statusText }}</p>

    <div class="pond" @mousemove="onPondMove" @click="onPondClick">
      <div class="caustic" />
      <div class="bubbles" aria-hidden="true">
        <i v-for="n in 12" :key="n" :style="{ '--i': n }" />
      </div>
      <div
        v-for="f in fishList"
        :key="f.fish_id"
        class="fish"
        :class="[face(f).cls, { flip: f.vx < 0 }]"
        :style="{
          left: (f.x / 1920) * 100 + '%',
          bottom: (f.y / 1080) * 100 + '%',
          width: face(f).size + 'px',
          height: face(f).size * 0.62 + 'px',
        }"
        :title="face(f).label"
      >
        <span class="eye" />
        <span class="fin" />
        <span class="tag">{{ face(f).label }}</span>
      </div>
      <FishCannon
        v-for="c in cannons"
        :key="'c' + c.seat"
        :left-pct="c.leftPct"
        :bottom-pct="c.bottomPct"
        :angle="c.angle"
        :mult="c.mult"
        :nickname="c.nickname"
        :me="c.me"
        :firing="c.firing"
        :empty="c.empty"
        :top="c.top"
      />
      <div
        class="cross"
        :style="{ left: (aimX / 1920) * 100 + '%', bottom: (aimY / 1080) * 100 + '%' }"
      />
      <div class="pond-hint" v-if="!roundId">渔场待命</div>
    </div>

    <div class="seats" v-if="seats.length">
      <div
        v-for="s in seats"
        :key="s.seat_id"
        class="seat"
        :class="{ me: s.seat_id === mySeat }"
      >
        <strong>{{ s.nickname || '座位' + s.seat_id }}</strong>
        <span>{{ s.ready ? '已准备' : '未准备' }}</span>
        <span v-if="!s.online" class="off">离线</span>
      </div>
    </div>

    <details class="log-box">
      <summary>日志</summary>
      <pre>{{ logs.join('\n') }}</pre>
    </details>
  </div>
</template>

<style scoped>
.ocean {
  min-height: 100vh;
  padding: 16px 18px 28px;
  background:
    radial-gradient(ellipse at top, rgba(30, 120, 160, 0.45), transparent 55%),
    linear-gradient(180deg, #062636 0%, #03151f 100%);
  color: #e8f4f8;
}
.topbar {
  display: flex;
  justify-content: space-between;
  gap: 14px;
  flex-wrap: wrap;
  margin-bottom: 10px;
}
.topbar h1 { margin: 0; font-size: 1.55rem; letter-spacing: 0.04em; }
.topbar p { margin: 4px 0 0; opacity: 0.8; font-size: 13px; }
.hud { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.gold-pill {
  display: inline-flex;
  align-items: baseline;
  gap: 6px;
  background: rgba(0, 0, 0, 0.28);
  border: 1px solid rgba(240, 209, 106, 0.4);
  border-radius: 999px;
  padding: 6px 12px;
  color: #f0d16a;
}
.gold-pill strong { font-size: 1.15rem; font-variant-numeric: tabular-nums; }
.gold-pill em { font-style: normal; color: #9be7c4; font-size: 12px; }
.mult {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: #9fc4d4;
}
.mult select {
  background: rgba(0, 0, 0, 0.35);
  color: #e8f4f8;
  border: 1px solid rgba(255, 255, 255, 0.18);
  border-radius: 6px;
  padding: 6px 8px;
}
.hud button {
  border: 0;
  border-radius: 8px;
  padding: 8px 12px;
  background: rgba(255, 255, 255, 0.1);
  color: #e8f4f8;
  cursor: pointer;
}
.hud button:disabled { opacity: 0.4; cursor: not-allowed; }
.hud .primary {
  background: linear-gradient(180deg, #3ec6b8, #1f8f84);
  color: #04201d;
  font-weight: 700;
}
.hud .ghost {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.2);
}
.status { margin: 0 0 10px; color: #9ad7c8; font-size: 13px; }
.pond {
  position: relative;
  height: min(68vh, 560px);
  border-radius: 18px;
  overflow: hidden;
  cursor: crosshair;
  background:
    radial-gradient(ellipse at 30% 20%, rgba(80, 200, 255, 0.18), transparent 45%),
    linear-gradient(180deg, #1a7ca3 0%, #0c4a66 45%, #072839 100%);
  box-shadow: inset 0 0 60px rgba(0, 0, 0, 0.35), 0 12px 32px rgba(0, 0, 0, 0.35);
  border: 1px solid rgba(120, 200, 230, 0.2);
}
.caustic {
  position: absolute;
  inset: 0;
  background:
    radial-gradient(circle at 20% 30%, rgba(255, 255, 255, 0.08), transparent 25%),
    radial-gradient(circle at 70% 60%, rgba(255, 255, 255, 0.06), transparent 30%);
  pointer-events: none;
  animation: shimmer 8s ease-in-out infinite alternate;
}
@keyframes shimmer {
  from { opacity: 0.55; transform: translateX(-1%); }
  to { opacity: 1; transform: translateX(1%); }
}
.bubbles {
  position: absolute;
  inset: 0;
  pointer-events: none;
  overflow: hidden;
}
.bubbles i {
  position: absolute;
  bottom: -20px;
  left: calc(var(--i) * 8%);
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.35);
  animation: rise calc(6s + var(--i) * 0.4s) linear infinite;
  animation-delay: calc(var(--i) * -0.5s);
}
@keyframes rise {
  to { transform: translateY(-120vh); opacity: 0; }
}
.fish {
  position: absolute;
  transform: translate(-50%, 50%);
  border-radius: 50% 45% 50% 45%;
  box-shadow: 0 4px 10px rgba(0, 0, 0, 0.28);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 2;
}
.fish.flip { transform: translate(-50%, 50%) scaleX(-1); }
.fish.flip .tag { transform: scaleX(-1); }
.fish.t1 { background: linear-gradient(180deg, #7ee8fa, #22a6c7); }
.fish.t2 { background: linear-gradient(180deg, #ffe08a, #e0a12b); }
.fish.t3 { background: linear-gradient(180deg, #ff9a6b, #d9480f); }
.fish.t0 { background: linear-gradient(180deg, #c4b5fd, #7c3aed); }
.eye {
  position: absolute;
  right: 22%;
  top: 32%;
  width: 18%;
  height: 28%;
  border-radius: 50%;
  background: #111;
  box-shadow: inset -1px -1px 0 #fff;
}
.fin {
  position: absolute;
  left: -10%;
  width: 22%;
  height: 55%;
  background: inherit;
  clip-path: polygon(100% 0, 0 50%, 100% 100%);
  filter: brightness(0.9);
}
.tag {
  position: absolute;
  bottom: -16px;
  font-size: 10px;
  color: rgba(255, 255, 255, 0.85);
  white-space: nowrap;
  text-shadow: 0 1px 2px #000;
  pointer-events: none;
}
.cross {
  position: absolute;
  width: 18px;
  height: 18px;
  transform: translate(-50%, 50%);
  border: 2px solid rgba(255, 220, 120, 0.7);
  border-radius: 50%;
  pointer-events: none;
  z-index: 6;
}
.cross::before, .cross::after {
  content: '';
  position: absolute;
  background: rgba(255, 220, 120, 0.7);
}
.cross::before { left: 50%; top: -4px; bottom: -4px; width: 1px; transform: translateX(-50%); }
.cross::after { top: 50%; left: -4px; right: -4px; height: 1px; transform: translateY(-50%); }
.pond-hint {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: rgba(232, 244, 248, 0.45);
  font-size: 1.1rem;
  letter-spacing: 0.12em;
  pointer-events: none;
}
.seats {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 12px;
}
.seat {
  background: rgba(0, 0, 0, 0.22);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 10px;
  padding: 8px 12px;
  font-size: 12px;
  display: flex;
  gap: 8px;
  align-items: center;
}
.seat.me { border-color: rgba(62, 198, 184, 0.55); }
.off { color: #ffb074; }
.log-box {
  margin-top: 12px;
  background: rgba(0, 0, 0, 0.22);
  border-radius: 10px;
  padding: 8px 12px;
  color: #9fc4d4;
  font-size: 12px;
}
.log-box summary { cursor: pointer; }
.log-box pre { margin: 8px 0 0; max-height: 140px; overflow: auto; white-space: pre-wrap; }
</style>