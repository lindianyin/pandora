import { computed, ref, type Ref } from 'vue'
import { loginGuest } from '../api'
import { GameSocket } from '../net/GameSocket'
import { cardLabel, sortDdzHand, type LobbyTemplate, type RoomSeat } from '../net/frame'

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'

export type DdzSettle = {
  round_id: number
  base_score: number
  multiplier: number
  entries: { uid: number; seat_id: number; delta_gold: number }[]
}

export type DdzLabClient = ReturnType<typeof createDdzLabClient>

export function createDdzLabClient(slot: number, deviceId: string) {
  const label = `P${slot + 1}`
  const nickname = ref('')
  const uid = ref(0)
  const gold = ref(0)
  const diamond = ref(0)
  const token = ref('')
  const wsOk = ref(false)
  const busy = ref(false)
  const logs = ref<string[]>([])
  const errorBanner = ref('')
  const templates = ref<LobbyTemplate[]>([])
  const matching = ref(false)
  const matchMsg = ref('')
  const roomId = ref(0)
  const roomPhase = ref('')
  const seats = ref<RoomSeat[]>([])
  const mySeat = ref(-1)
  const hand = ref<number[]>([])
  const selected = ref<number[]>([])
  const turnSeat = ref(-1)
  const turnPhase = ref('')
  const countdown = ref(0)
  const landlordSeat = ref(-1)
  const bottom = ref<number[]>([])
  const cardsLeft = ref<number[]>([0, 0, 0])
  const lastPlays = ref<Record<number, string>>({})
  const ddzRoundId = ref(0)
  const settle = ref<DdzSettle | null>(null)
  const showSettle = ref(false)

  let sock: GameSocket | null = null
  let countdownTimer: number | null = null

  function trace(event: string, detail: string) {
    sock?.trace(ddzRoundId.value, mySeat.value, 'ddz', event, detail)
  }

  function pushLog(line: string) {
    const t = new Date().toLocaleTimeString()
    logs.value = [`[${t}] ${line}`, ...logs.value].slice(0, 40)
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
    }, 3000)
  }

  const isMyTurn = computed(() => turnSeat.value === mySeat.value && mySeat.value >= 0)
  const seatSelf = computed(() => seats.value.find((s) => s.seat_id === mySeat.value) || null)
  const iAmReady = computed(() => !!seatSelf.value?.ready)
  const canReady = computed(() => {
    const phase = roomPhase.value || turnPhase.value || ''
    if (phase === 'Play' || phase === 'Bid' || phase === 'Deal') return false
    return !!roomId.value
  })
  const isBidTurn = computed(() => turnPhase.value === 'Bid' && isMyTurn.value)
  const isPlayTurn = computed(() => turnPhase.value === 'Play' && isMyTurn.value)

  function createSocket(): GameSocket {
    return new GameSocket({
      onLog: pushLog,
      onAuth: (ok, u) => {
        wsOk.value = ok
        if (ok) {
          pushLog(`authed uid=${u}`)
          sock?.getLobby()
        }
      },
      onClose: () => {
        wsOk.value = false
        pushLog('ws closed')
      },
      onLobby: (info) => {
        templates.value = info.templates
        gold.value = info.gold
        diamond.value = info.diamond
      },
      onMatchStatus: (m) => {
        if (m.status === 1) {
          matching.value = true
          matchMsg.value = m.message || 'matching'
        } else if (m.status === 2) {
          matching.value = false
          matchMsg.value = ''
          roomId.value = m.room_id
          pushLog(`matched room=${m.room_id}`)
        } else if (m.status === 3) {
          matching.value = false
          matchMsg.value = m.message || 'timeout'
        } else {
          matching.value = false
        }
      },
      onRoomState: (rs) => {
        roomId.value = rs.room_id
        seats.value = rs.seats
        roomPhase.value = rs.phase
        const me = rs.seats.find((s) => s.uid === uid.value)
        if (me) mySeat.value = me.seat_id
      },
      onGameStart: (g) => {
        if (g.round_id) ddzRoundId.value = g.round_id
        mySeat.value = g.seat_id
        hand.value = sortDdzHand(g.hand_cards)
        landlordSeat.value = g.landlord_seat
        bottom.value = [...g.bottom_cards]
        selected.value = []
        showSettle.value = false
        settle.value = null
        lastPlays.value = {}
        roomPhase.value = g.landlord_seat < 0 ? 'Bid' : 'Play'
        const next = [17, 17, 17]
        next[g.seat_id] = g.hand_cards.length
        if (g.landlord_seat >= 0) {
          next[g.landlord_seat] = g.landlord_seat === g.seat_id ? g.hand_cards.length : 20
        }
        cardsLeft.value = next
        pushLog(`deal seat=${g.seat_id} landlord=${g.landlord_seat} hand=${g.hand_cards.length}`)
        trace(g.landlord_seat < 0 ? 'deal' : 'play_start', `seat=${g.seat_id} landlord=${g.landlord_seat} hand=${g.hand_cards.join(',')} bottom=${g.bottom_cards.join(',')}`)
      },
      onTurn: (t) => {
        turnSeat.value = t.seat_id
        turnPhase.value = t.phase
        roomPhase.value = t.phase
        startCountdown(t.timeout_s)
        trace('turn', `seat=${t.seat_id} phase=${t.phase} timeout=${t.timeout_s}`)
      },
      onBidBroadcast: (b) => {
        lastPlays.value = { ...lastPlays.value, [b.seat_id]: b.score === 0 ? '不叫' : `叫${b.score}分` }
        pushLog(`seat ${b.seat_id} bid ${b.score}`)
        trace('bid', `seat=${b.seat_id} score=${b.score}`)
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
          const remove = new Set(p.cards)
          hand.value = sortDdzHand(
            hand.value.filter((c) => {
              if (remove.has(c)) {
                remove.delete(c)
                return false
              }
              return true
            }),
          )
          selected.value = []
        }
        pushLog(p.pass ? `seat ${p.seat_id} pass` : `seat ${p.seat_id} play ${p.cards.map(cardLabel).join(' ')}`)
        trace(p.pass ? 'pass' : 'play', `seat=${p.seat_id} cards=${p.cards.join(',')} left=${p.cards_left}`)
      },
      onSettle: (s) => {
        settle.value = s
        showSettle.value = true
        clearCountdown()
        turnPhase.value = ''
        roomPhase.value = 'WaitReady'
        for (const e of s.entries) {
          if (e.uid === uid.value) gold.value += e.delta_gold
        }
        if (s.round_id) ddzRoundId.value = s.round_id
        pushLog(`settle base=${s.base_score} x${s.multiplier}`)
        trace('settle', `base=${s.base_score} mult=${s.multiplier}`)
        sock?.getLobby()
      },
      onReconnect: (r) => {
        mySeat.value = r.seat_id
        hand.value = sortDdzHand(r.hand)
        selected.value = []
        landlordSeat.value = r.landlord_seat
        turnSeat.value = r.current_seat
        turnPhase.value = r.phase
        roomPhase.value = r.phase
        const next = [...cardsLeft.value]
        next[r.seat_id] = r.hand.length
        cardsLeft.value = next
        startCountdown(r.timeout_s)
        pushLog(`reconnect hand=${r.hand.length} phase=${r.phase} left=${r.timeout_s}`)
        trace('reconnect', `phase=${r.phase} turn=${r.current_seat} left=${r.timeout_s} hand=${r.hand.join(',')}`)
      },
      onError: (e) => {
        const msg = e.message || `err ${e.code}`
        errorBanner.value = msg
        clearErrorSoon()
        pushLog(`err: ${msg}`)
        trace('error', `code=${e.code} ref=${e.ref_msg_id} ${msg}`)
      },
    })
  }

  async function login() {
    busy.value = true
    errorBanner.value = ''
    try {
      const r = await loginGuest(deviceId)
      if (r.code !== 0) {
        errorBanner.value = r.message || 'login failed'
        return
      }
      token.value = r.data.access_token
      uid.value = r.data.uid
      nickname.value = r.data.nickname
      gold.value = r.data.gold
      diamond.value = r.data.diamond
      sock?.close()
      sock = createSocket()
      sock.connect(WSS_URL, token.value)
      pushLog(`login ok ${nickname.value} device=${deviceId}`)
    } catch (e) {
      errorBanner.value = String(e)
    } finally {
      busy.value = false
    }
  }

  function ddzTemplateId(): number {
    const t = templates.value.find((x) => (x.game_id === 2000 || x.game_id === 1) && x.enabled)
    return t?.id ?? 1
  }

  function matchDdz() {
    matching.value = true
    matchMsg.value = 'matching'
    sock?.quickMatch(ddzTemplateId())
  }

  function doReady() {
    showSettle.value = false
    sock?.ready(true)
  }

  function toggleCard(c: number) {
    if (selected.value.includes(c)) selected.value = selected.value.filter((x) => x !== c)
    else selected.value = [...selected.value, c]
  }

  function doBid(score: number) {
    trace('cmd_bid', `score=${score}`)
    sock?.bid(score)
  }

  function doPlay() {
    if (!selected.value.length) return
    trace('cmd_play', `cards=${selected.value.join(',')}`)
    sock?.play(false, selected.value)
  }

  function doPass() {
    trace('cmd_pass', '')
    sock?.play(true, [])
  }

  function leave() {
    if (matching.value) {
      sock?.cancelMatch()
      matching.value = false
      matchMsg.value = ''
    }
    sock?.leaveRoom()
    roomId.value = 0
    roomPhase.value = ''
    turnPhase.value = ''
    ddzRoundId.value = 0
    hand.value = []
    selected.value = []
    bottom.value = []
    landlordSeat.value = -1
  }

  function dispose() {
    clearCountdown()
    sock?.close()
    sock = null
  }

  return {
    slot,
    label,
    deviceId,
    nickname,
    uid,
    gold,
    diamond,
    wsOk,
    busy,
    logs,
    errorBanner,
    matching,
    matchMsg,
    roomId,
    roomPhase,
    seats,
    mySeat,
    hand,
    selected,
    turnSeat,
    turnPhase,
    countdown,
    landlordSeat,
    bottom,
    cardsLeft,
    lastPlays,
    ddzRoundId,
    settle,
    showSettle,
    isMyTurn,
    iAmReady,
    canReady,
    isBidTurn,
    isPlayTurn,
    login,
    matchDdz,
    doReady,
    toggleCard,
    doBid,
    doPlay,
    doPass,
    leave,
    dispose,
    cardLabel,
  }
}

export type DdzLabClientRefs = {
  [K in keyof DdzLabClient]: DdzLabClient[K] extends Ref<infer V> ? V : DdzLabClient[K]
}
