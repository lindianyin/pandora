import { computed, ref } from 'vue'
import { loginGuest, health } from '../api'
import { GameSocket } from '../net/GameSocket'
import { cardLabel, type LobbyTemplate, type RoomSeat } from '../net/frame'
import router from '../router'

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'

export type SettleInfo = {
  round_id: number
  base_score: number
  multiplier: number
  entries: { uid: number; seat_id: number; delta_gold: number }[]
}

const nickname = ref('')
const uid = ref(0)
const gold = ref(0)
const diamond = ref(0)
const token = ref('')
const wsOk = ref(false)
const logs = ref<string[]>([])
const busy = ref(false)
const templates = ref<LobbyTemplate[]>([])
const matching = ref(false)
const matchMsg = ref('')
const matchTimedOut = ref(false)
const roomId = ref(0)
const seats = ref<RoomSeat[]>([])
const roomPhase = ref('')
const mySeat = ref(-1)
const hand = ref<number[]>([])
const selected = ref<number[]>([])
const landlordSeat = ref(-1)
const bottom = ref<number[]>([])
const turnSeat = ref(-1)
const turnPhase = ref('')
const timeoutS = ref(0)
const countdown = ref(0)
const settle = ref<SettleInfo | null>(null)
const showSettle = ref(false)
const errorBanner = ref('')
const cardsLeft = ref<number[]>([0, 0, 0])
const lastPlays = ref<Record<number, string>>({})
const reconnecting = ref(false)
const reconnectHint = ref('')
const activityTick = {
  listeners: [] as Array<() => void>,
  subscribe(fn: () => void) {
    this.listeners.push(fn)
    return () => {
      this.listeners = this.listeners.filter((x) => x !== fn)
    }
  },
  notify() {
    for (const fn of this.listeners) fn()
  },
}

let sock: GameSocket | null = null
let countdownTimer: number | null = null
let reconnectTimer: number | null = null
let reconnectAttempts = 0

function pushLog(line: string) {
  const t = new Date().toLocaleTimeString()
  logs.value = [`[${t}] ${line}`, ...logs.value].slice(0, 100)
}

function clearCountdown() {
  if (countdownTimer != null) {
    clearInterval(countdownTimer)
    countdownTimer = null
  }
}

function startCountdown(seconds: number) {
  clearCountdown()
  countdown.value = seconds
  countdownTimer = window.setInterval(() => {
    if (countdown.value <= 0) {
      clearCountdown()
      return
    }
    countdown.value -= 1
  }, 1000)
}

function clearErrorSoon() {
  window.setTimeout(() => {
    errorBanner.value = ''
  }, 3500)
}

function scheduleReconnect() {
  if (!token.value) return
  if (reconnectTimer != null) return
  reconnecting.value = true
  const delay = Math.min(8000, 1000 + reconnectAttempts * 1000)
  reconnectHint.value = `连接断开，${Math.round(delay / 1000)}s 后重连…`
  pushLog(reconnectHint.value)
  reconnectTimer = window.setTimeout(() => {
    reconnectTimer = null
    reconnectAttempts += 1
    if (!token.value) return
    pushLog(`auto reconnect attempt=${reconnectAttempts}`)
    sock = createSocket()
    sock.connect(WSS_URL, token.value)
  }, delay)
}

function createSocket(): GameSocket {
  return new GameSocket({
    onLog: pushLog,
    onAuth: (ok, u) => {
      wsOk.value = ok
      if (ok) {
        reconnecting.value = false
        reconnectHint.value = ''
        reconnectAttempts = 0
        pushLog(`ws authed uid=${u}`)
        sock?.getLobby()
        const path = router.currentRoute.value.path
        if (path === '/login' || path === '/') {
          router.push('/lobby')
        }
      } else {
        errorBanner.value = '鉴权失败，请重新登录'
        clearErrorSoon()
      }
    },
    onLobby: (info) => {
      templates.value = info.templates
      gold.value = info.gold
      diamond.value = info.diamond
    },
    onMatchStatus: (m) => {
      matchTimedOut.value = false
      if (m.status === 0) {
        matching.value = true
        matchMsg.value = m.message || '匹配中…'
      } else if (m.status === 1) {
        matching.value = false
        matchMsg.value = ''
        roomId.value = m.room_id
        settle.value = null
        showSettle.value = false
        hand.value = []
        selected.value = []
        lastPlays.value = {}
        turnPhase.value = ''
        turnSeat.value = -1
        roomPhase.value = 'WaitReady'
        landlordSeat.value = -1
        bottom.value = []
        countdown.value = 0
        clearCountdown()
        router.push({ path: '/table', query: { room_id: String(m.room_id) } })
      } else if (m.status === 2) {
        matching.value = false
        matchTimedOut.value = true
        matchMsg.value = m.message || '匹配超时，请重试'
      } else {
        matching.value = false
        matchMsg.value = m.message || '已取消'
      }
    },
    onRoomState: (rs) => {
      roomId.value = rs.room_id
      seats.value = rs.seats
      roomPhase.value = rs.phase
      const me = rs.seats.find((x) => Number(x.uid) === Number(uid.value))
      if (me) mySeat.value = me.seat_id
      if (mySeat.value < 0 && rs.seats.length) {
        const byUid = rs.seats.find((x) => Number(x.uid) === Number(uid.value))
        if (byUid) mySeat.value = byUid.seat_id
      }
      if (rs.phase === 'WaitReady') {
        turnPhase.value = ''
        turnSeat.value = -1
        showSettle.value = false
        clearCountdown()
      }
      if (rs.room_id && router.currentRoute.value.path !== '/table') {
        router.push({ path: '/table', query: { room_id: String(rs.room_id) } })
      }
    },
    onGameStart: (g) => {
      mySeat.value = g.seat_id
      hand.value = [...g.hand_cards]
      landlordSeat.value = g.landlord_seat
      bottom.value = [...g.bottom_cards]
      selected.value = []
      showSettle.value = false
      settle.value = null
      lastPlays.value = {}
      const next = [...cardsLeft.value]
      for (let i = 0; i < 3; ++i) {
        if (i === g.seat_id) next[i] = g.hand_cards.length
        else if (!next[i]) next[i] = 17
      }
      if (g.landlord_seat >= 0) {
        next[g.landlord_seat] = g.landlord_seat === g.seat_id ? g.hand_cards.length : 20
      }
      cardsLeft.value = next
    },
    onTurn: (t) => {
      turnSeat.value = t.seat_id
      turnPhase.value = t.phase
      timeoutS.value = t.timeout_s
      startCountdown(t.timeout_s)
    },
    onBidBroadcast: (b) => {
      lastPlays.value = { ...lastPlays.value, [b.seat_id]: b.score === 0 ? '不叫' : `叫${b.score}分` }
      pushLog(`seat ${b.seat_id} bid ${b.score}`)
    },
    onPlayBroadcast: (p) => {
      const next = [...cardsLeft.value]
      next[p.seat_id] = p.cards_left
      cardsLeft.value = next
      lastPlays.value = {
        ...lastPlays.value,
        [p.seat_id]: p.pass ? '过' : p.cards.map(cardLabel).join(' '),
      }
      if (p.seat_id === mySeat.value && !p.pass) {
        hand.value = hand.value.filter((c) => !p.cards.includes(c))
        selected.value = []
      }
      pushLog(
        p.pass ? `seat ${p.seat_id} pass` : `seat ${p.seat_id} play ${p.cards.map(cardLabel).join(' ')}`,
      )
    },
    onSettle: (s) => {
      settle.value = s
      showSettle.value = true
      clearCountdown()
      pushLog(`settle base=${s.base_score} x${s.multiplier}`)
      sock?.getLobby()
    },
    onReconnect: (r) => {
      mySeat.value = r.seat_id
      hand.value = [...r.hand]
      selected.value = []
      landlordSeat.value = r.landlord_seat
      turnSeat.value = r.current_seat
      turnPhase.value = r.phase
      roomPhase.value = r.phase
      const next = [...cardsLeft.value]
      next[r.seat_id] = r.hand.length
      cardsLeft.value = next
      startCountdown(r.timeout_s)
      reconnectHint.value = '已重连，手牌已恢复'
      pushLog(`reconnect restored hand=${r.hand.length} phase=${r.phase}`)
      if (router.currentRoute.value.path !== '/table' && roomId.value) {
        router.push({ path: '/table', query: { room_id: String(roomId.value) } })
      }
    },
    onError: (e) => {
      errorBanner.value = e.message || `错误 ${e.code}`
      clearErrorSoon()
      pushLog(`err: ${e.message}`)
    },
    onActivityUpdate: (a) => {
      pushLog(`activity ${a.activity_id} ${a.type} claimable=${a.claimable}`)
      activityTick.notify()
    },
    onKick: () => {
      errorBanner.value = '已被踢下线，尝试重连…'
      wsOk.value = false
      scheduleReconnect()
    },
    onClose: () => {
      wsOk.value = false
      scheduleReconnect()
    },
  })
}

const isMyTurn = computed(() => turnSeat.value === mySeat.value && mySeat.value >= 0)

const seatLeft = computed(
  () => (mySeat.value < 0 ? null : seats.value.find((s) => s.seat_id === (mySeat.value + 2) % 3) || null),
)
const seatRight = computed(
  () => (mySeat.value < 0 ? null : seats.value.find((s) => s.seat_id === (mySeat.value + 1) % 3) || null),
)
const seatSelf = computed(
  () => (mySeat.value < 0 ? null : seats.value.find((s) => s.seat_id === mySeat.value) || null),
)

const iAmReady = computed(() => !!seatSelf.value?.ready)

const canReady = computed(() => {
  const phase = roomPhase.value || ''
  const turn = turnPhase.value || ''
  if (turn === 'Bid' || turn === 'Play') return false
  if (phase === 'Bid' || phase === 'Play') return false
  return true
})

async function checkHealth() {
  try {
    const h = await health()
    pushLog(`health: ${JSON.stringify(h.data ?? h)}`)
  } catch (e) {
    pushLog(`health failed: ${e}`)
  }
}

async function loginAndConnect() {
  busy.value = true
  errorBanner.value = ''
  try {
    const deviceId =
      sessionStorage.getItem('pandora_device') || `web-${Math.random().toString(16).slice(2)}`
    sessionStorage.setItem('pandora_device', deviceId)
    const r = await loginGuest(deviceId)
    if (r.code !== 0) {
      pushLog(`login failed: ${r.message}`)
      errorBanner.value = r.message || '登录失败'
      return
    }
    token.value = r.data.access_token
    uid.value = r.data.uid
    nickname.value = r.data.nickname
    gold.value = r.data.gold
    diamond.value = r.data.diamond
    sessionStorage.setItem('pandora_token', token.value)
    pushLog(`login ok uid=${uid.value}`)

    if (reconnectTimer != null) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
    reconnectAttempts = 0
    sock?.close()
    sock = createSocket()
    sock.connect(WSS_URL, token.value)
  } catch (e) {
    pushLog(`login error: ${e}`)
    errorBanner.value = String(e)
  } finally {
    busy.value = false
  }
}

function refreshLobby() {
  sock?.getLobby()
}

function quickMatch(templateId: number) {
  matchTimedOut.value = false
  matchMsg.value = '匹配中…'
  matching.value = true
  sock?.quickMatch(templateId)
}

function cancelMatch() {
  sock?.cancelMatch()
  matching.value = false
  matchTimedOut.value = false
  matchMsg.value = ''
}

function doReady() {
  showSettle.value = false
  if (!sock || !wsOk.value) {
    errorBanner.value = '未连接服务器，请重新登录'
    clearErrorSoon()
    pushLog('ready failed: no socket')
    return
  }
  pushLog('send Ready')
  sock.ready(true)
}

function doBid(score: number) {
  sock?.bid(score)
}

function toggleCard(c: number) {
  if (selected.value.includes(c)) selected.value = selected.value.filter((x) => x !== c)
  else selected.value = [...selected.value, c]
}

function doPlay() {
  sock?.play(false, selected.value)
}

function doPass() {
  sock?.play(true, [])
}

function backLobby() {
  sock?.leaveRoom()
  showSettle.value = false
  settle.value = null
  roomId.value = 0
  hand.value = []
  turnPhase.value = ''
  clearCountdown()
  sock?.getLobby()
  router.push('/lobby')
}

function closeSettleStay() {
  showSettle.value = false
}

function disconnect() {
  clearCountdown()
  if (reconnectTimer != null) {
    clearTimeout(reconnectTimer)
    reconnectTimer = null
  }
  token.value = ''
  sessionStorage.removeItem('pandora_token')
  sock?.close()
  sock = null
  wsOk.value = false
}

function applyBalances(g: number, d: number) {
  gold.value = g
  diamond.value = d
}

export function useGameSession() {
  return {
    nickname,
    uid,
    gold,
    diamond,
    token,
    wsOk,
    logs,
    busy,
    templates,
    matching,
    matchMsg,
    matchTimedOut,
    roomId,
    seats,
    roomPhase,
    mySeat,
    hand,
    selected,
    landlordSeat,
    bottom,
    turnSeat,
    turnPhase,
    timeoutS,
    countdown,
    settle,
    showSettle,
    errorBanner,
    cardsLeft,
    lastPlays,
    reconnecting,
    reconnectHint,
    isMyTurn,
    iAmReady,
    canReady,
    seatLeft,
    seatRight,
    seatSelf,
    checkHealth,
    loginAndConnect,
    refreshLobby,
    quickMatch,
    cancelMatch,
    doReady,
    doBid,
    toggleCard,
    doPlay,
    doPass,
    backLobby,
    closeSettleStay,
    disconnect,
    applyBalances,
    cardLabel,
    pushLog,
    activityTick,
  }
}

export type { LobbyTemplate, RoomSeat }

