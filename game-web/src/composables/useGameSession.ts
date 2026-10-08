import { computed, ref } from 'vue'
import { loginGuest, health } from '../api'
import { GameSocket } from '../net/GameSocket'
import { cardLabel, sortDdzHand, tileLabel, type LobbyTemplate, type RoomSeat, type HzmjSettle } from '../net/frame'
import { canChiClaim, canMingGangClaim, canPengClaim, listChiOptions } from '../net/hzmjMeld'
import {
  applySelfDiscard,
  canHuHand,
  canShowDianpaoHu,
  listAnGangTiles,
  listBuGangTiles,
  removeOneTile,
  sortHand,
} from '../net/hzmjHand'
import {
  applyMeldBroadcast,
  meldKindLabel,
  pushDiscardRiver,
  restoreDiscardRiver,
  takeClaimedFromRiver,
  type HzmjMeld,
} from '../net/hzmjFront'
import router from '../router'

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'

export type SettleInfo = {
  round_id: number
  base_score: number
  multiplier: number
  entries: { uid: number; seat_id: number; delta_gold: number }[]
}

const ACTION_LABELS = ['过', '吃', '碰', '杠', '胡']

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
const roomTemplateId = ref(0)
const currentGameId = ref(1)
const seats = ref<RoomSeat[]>([])
const roomPhase = ref('')
const mySeat = ref(-1)
const hand = ref<number[]>([])
const selected = ref<number[]>([])
const selectedHandIndex = ref<number | null>(null)
const landlordSeat = ref(-1)
const bottom = ref<number[]>([])
const turnSeat = ref(-1)
const turnPhase = ref('')
const timeoutS = ref(0)
const countdown = ref(0)
const settle = ref<SettleInfo | null>(null)
const showSettle = ref(false)
const errorBanner = ref('')
const cardsLeft = ref<number[]>([0, 0, 0, 0])
const lastPlays = ref<Record<number, string>>({})
const reconnecting = ref(false)
const reconnectHint = ref('')

const hzmjCaishen = ref<number[]>([33])
const hzmjBanker = ref(0)
const hzmjLian = ref(1)
const hzmjN = ref(2)
const hzmjWall = ref(0)
const hzmjRoundId = ref(0)
const ddzRoundId = ref(0)
const hzmjSub = ref('')
const hzmjPiaoSeat = ref(-1)
const hzmjLastDiscard = ref<{ seat: number; tile: number } | null>(null)
const hzmjMelds = ref<Record<number, HzmjMeld[]>>({})
const hzmjRivers = ref<Record<number, number[]>>({})
const hzmjSettle = ref<HzmjSettle | null>(null)
const showHzmjSettle = ref(false)
const showLiuJu = ref(false)
const hzmjBaseScore = ref(100)
const hzmjClaimSent = ref(false)
const hzmjClaimHint = ref('')

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
let replayingSnapshot = false

function trace(game: 'hzmj' | 'ddz', roundId: number, event: string, detail: string) {
  sock?.trace(roundId, mySeat.value, game, event, detail)
}

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

function resolveGameId(templateId: number): number {
  const t = templates.value.find((x) => x.id === templateId)
  if (t?.game_id) return t.game_id
  if (templateId === 2) return 2
  if (templateId === 3) return 3
  return 1
}

function tablePathForGame(gameId: number) {
  if (gameId === 2) return '/hzmj-table'
  if (gameId === 3) return '/phz-table'
  return '/table'
}

function goTable(room: number, gameId: number) {
  currentGameId.value = gameId
  const path = tablePathForGame(gameId)
  if (router.currentRoute.value.path !== path) {
    router.push({ path, query: { room_id: String(room) } })
  }
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
        hzmjSettle.value = null
        showHzmjSettle.value = false
        showLiuJu.value = false
        hand.value = []
        selected.value = []
        selectedHandIndex.value = null
        lastPlays.value = {}
        hzmjMelds.value = {}
        hzmjRivers.value = {}
        turnPhase.value = ''
        hzmjSub.value = ''
        turnSeat.value = -1
        roomPhase.value = 'WaitReady'
        landlordSeat.value = -1
        bottom.value = []
        countdown.value = 0
        clearCountdown()
        goTable(m.room_id, currentGameId.value)
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
      roomTemplateId.value = rs.template_id
      seats.value = rs.seats
      roomPhase.value = rs.phase
      const gid = resolveGameId(rs.template_id)
      currentGameId.value = gid
      const me = rs.seats.find((x) => Number(x.uid) === Number(uid.value))
      if (me) mySeat.value = me.seat_id
      if (rs.phase === 'WaitReady') {
        turnPhase.value = ''
        hzmjSub.value = ''
        turnSeat.value = -1
        showSettle.value = false
        showHzmjSettle.value = false
        clearCountdown()
      }
      if (rs.room_id) goTable(rs.room_id, gid)
    },
    onGameStart: (g) => {
      currentGameId.value = 1
      if (g.round_id) ddzRoundId.value = g.round_id
      mySeat.value = g.seat_id
      hand.value = sortDdzHand(g.hand_cards)
      landlordSeat.value = g.landlord_seat
      bottom.value = [...g.bottom_cards]
      selected.value = []
      showSettle.value = false
      settle.value = null
      lastPlays.value = {}
      const next = [0, 0, 0, 0]
      for (let i = 0; i < 3; ++i) {
        if (i === g.seat_id) next[i] = g.hand_cards.length
        else next[i] = 17
      }
      if (g.landlord_seat >= 0) {
        next[g.landlord_seat] = g.landlord_seat === g.seat_id ? g.hand_cards.length : 20
      }
      cardsLeft.value = next
      trace('ddz', ddzRoundId.value, g.landlord_seat < 0 ? 'deal' : 'play_start', `seat=${g.seat_id} landlord=${g.landlord_seat} hand=${g.hand_cards.join(',')} bottom=${g.bottom_cards.join(',')}`)
    },
    onTurn: (t) => {
      turnSeat.value = t.seat_id
      turnPhase.value = t.phase
      timeoutS.value = t.timeout_s
      startCountdown(t.timeout_s)
      trace('ddz', ddzRoundId.value, 'turn', `seat=${t.seat_id} phase=${t.phase} timeout=${t.timeout_s}`)
    },
    onBidBroadcast: (b) => {
      lastPlays.value = { ...lastPlays.value, [b.seat_id]: b.score === 0 ? '不叫' : `叫${b.score}分` }
      pushLog(`seat ${b.seat_id} bid ${b.score}`)
      trace('ddz', ddzRoundId.value, 'bid', `seat=${b.seat_id} score=${b.score}`)
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
        hand.value = sortDdzHand(hand.value.filter((c) => !p.cards.includes(c)))
        selected.value = []
      }
      pushLog(
        p.pass ? `seat ${p.seat_id} pass` : `seat ${p.seat_id} play ${p.cards.map(cardLabel).join(' ')}`,
      )
      trace('ddz', ddzRoundId.value, p.pass ? 'pass' : 'play', `seat=${p.seat_id} cards=${p.cards.join(',')} left=${p.cards_left}`)
    },
    onSettle: (s) => {
      settle.value = s
      showSettle.value = true
      clearCountdown()
      pushLog(`settle base=${s.base_score} x${s.multiplier}`)
      if (s.round_id) ddzRoundId.value = s.round_id
      trace('ddz', s.round_id, 'settle', `base=${s.base_score} mult=${s.multiplier}`)
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
      reconnectHint.value = '已重连，手牌已恢复'
      pushLog(`reconnect restored hand=${r.hand.length} phase=${r.phase}`)
      trace('ddz', ddzRoundId.value, 'reconnect', `phase=${r.phase} turn=${r.current_seat} left=${r.timeout_s} hand=${r.hand.join(',')}`)
      if (roomId.value) goTable(roomId.value, 1)
    },
    onHzmjGameStart: (g) => {
      currentGameId.value = 2
      const sameRound = hzmjRoundId.value !== 0 && g.round_id === hzmjRoundId.value
      hzmjRoundId.value = g.round_id
      roomId.value = g.room_id || roomId.value
      roomTemplateId.value = g.template_id
      mySeat.value = g.self_seat
      hand.value = sortHand(g.self_hand)
      selectedHandIndex.value = null
      hzmjCaishen.value = g.caishen.length ? [...g.caishen] : [33]
      hzmjBanker.value = g.banker_seat
      hzmjLian.value = g.lian_zhuang
      hzmjN.value = g.N
      hzmjWall.value = g.wall_remain
      hzmjBaseScore.value = g.base_score
      hzmjMelds.value = {}
      replayingSnapshot = sameRound
      if (!sameRound) {
        hzmjRivers.value = {}
        hzmjLastDiscard.value = null
        lastPlays.value = {}
      }
      showHzmjSettle.value = false
      showLiuJu.value = false
      hzmjSettle.value = null
      showSettle.value = false
      hzmjClaimSent.value = false
      hzmjClaimHint.value = ''
      roomPhase.value = 'Play'
      if (sameRound) {
        const next = [...cardsLeft.value]
        next[g.self_seat] = g.self_hand.length
        cardsLeft.value = next
      } else {
        const next = [13, 13, 13, 13]
        next[g.banker_seat] = 14
        next[g.self_seat] = g.self_hand.length
        cardsLeft.value = next
      }
      hzmjCanZimo.value = false
      goTable(roomId.value, 2)
      pushLog(`hzmj start banker=${g.banker_seat} N=${g.N} hand=${g.self_hand.length}`)
      trace('hzmj', hzmjRoundId.value, sameRound ? 'resync' : 'start', `banker=${g.banker_seat} N=${g.N} wall=${g.wall_remain} hand=${g.self_hand.join(',')}`)
    },
    onHzmjTurn: (t) => {
      turnSeat.value = t.seat_id
      hzmjSub.value = t.sub
      hzmjCanZimo.value = !!t.can_zimo && t.seat_id === mySeat.value
      turnPhase.value = t.sub
      hzmjWall.value = t.wall_remain
      hzmjPiaoSeat.value = t.piao_seat
      timeoutS.value = t.timeout_s
      startCountdown(t.timeout_s)
      if (t.self_hand && t.self_hand.length > 0) {
        hand.value = sortHand(t.self_hand)
        selectedHandIndex.value = null
        const next = [...cardsLeft.value]
        next[mySeat.value] = t.self_hand.length
        cardsLeft.value = next
        pushLog(`hzmj hand sync len=${t.self_hand.length}`)
      }
      if (t.sub === 'claim') {
        hzmjClaimSent.value = false
        hzmjClaimHint.value = '请选择鸣牌或过（仅三牢且庄闲可点炮）'
      } else {
        hzmjClaimSent.value = false
        hzmjClaimHint.value = ''
        selectedHandIndex.value = null
      }
      replayingSnapshot = false
      trace('hzmj', hzmjRoundId.value, 'turn', `seat=${t.seat_id} sub=${t.sub} timeout=${t.timeout_s} wall=${t.wall_remain} hand=${(t.self_hand || []).join(',')}`)
    },
    onHzmjDraw: (d) => {
      hzmjClaimSent.value = false
      hzmjClaimHint.value = ''
      if (d.seat_id === mySeat.value && d.tile >= 0) {
        hand.value = sortHand([...hand.value, d.tile])
        selectedHandIndex.value = hand.value.lastIndexOf(d.tile)
      }
      const next = [...cardsLeft.value]
      if (typeof next[d.seat_id] === 'number') next[d.seat_id]! += 1
      cardsLeft.value = next
      if (hzmjWall.value > 0) hzmjWall.value -= 1
      lastPlays.value = { ...lastPlays.value, [d.seat_id]: '摸牌' }
      trace('hzmj', hzmjRoundId.value, 'draw', `seat=${d.seat_id} tile=${d.tile}`)
    },
    onHzmjDiscard: (d) => {
      hzmjClaimSent.value = false
      hzmjLastDiscard.value = { seat: d.seat_id, tile: d.tile }
      lastPlays.value = { ...lastPlays.value, [d.seat_id]: tileLabel(d.tile) }
      const rivers = { ...hzmjRivers.value }
      const prev = rivers[d.seat_id] || []
      rivers[d.seat_id] = replayingSnapshot
        ? restoreDiscardRiver(prev, d.tile)
        : pushDiscardRiver(prev, d.tile)
      hzmjRivers.value = rivers
      if (replayingSnapshot) {
        pushLog(`seat ${d.seat_id} discard ${tileLabel(d.tile)}`)
        trace('hzmj', hzmjRoundId.value, 'discard', `seat=${d.seat_id} tile=${d.tile}`)
        return
      }
      if (d.seat_id === mySeat.value) {
        hzmjSub.value = ''
        const r = applySelfDiscard(hand.value, d.tile)
        if (r.changed) {
          hand.value = r.hand
          selectedHandIndex.value = null
          const next = [...cardsLeft.value]
          next[mySeat.value] = hand.value.length
          cardsLeft.value = next
        }
      } else {
        const next = [...cardsLeft.value]
        if (typeof next[d.seat_id] === 'number' && next[d.seat_id]! > 0) next[d.seat_id]! -= 1
        cardsLeft.value = next
      }
      pushLog(`seat ${d.seat_id} discard ${tileLabel(d.tile)}`)
      trace('hzmj', hzmjRoundId.value, 'discard', `seat=${d.seat_id} tile=${d.tile}`)
    },
    onHzmjAction: (a) => {
      hzmjClaimSent.value = false
      hzmjClaimHint.value = ''
      const kind = a.meld_kind || a.action
      const label = meldKindLabel(kind) || ACTION_LABELS[a.action] || `act${a.action}`
      const text = a.tile >= 0 ? `${label} ${tileLabel(a.tile)}` : label
      lastPlays.value = { ...lastPlays.value, [a.seat_id]: text }
      if (a.action >= 1 && a.action <= 3) {
        const melds = { ...hzmjMelds.value }
        melds[a.seat_id] = applyMeldBroadcast(
          melds[a.seat_id] || [],
          a.action,
          a.tile,
          a.tiles || [],
          a.meld_kind || 0,
          a.from_seat ?? -1,
        )
        hzmjMelds.value = melds
        if (!replayingSnapshot && a.from_seat >= 0 && a.tile >= 0 && kind !== 4) {
          const rivers = { ...hzmjRivers.value }
          rivers[a.from_seat] = takeClaimedFromRiver(rivers[a.from_seat] || [], a.tile)
          hzmjRivers.value = rivers
        }
      }
      if (replayingSnapshot) {
        pushLog(`seat ${a.seat_id} ${text}`)
        trace('hzmj', hzmjRoundId.value, 'action', `seat=${a.seat_id} act=${a.action} kind=${a.meld_kind} tile=${a.tile} from=${a.from_seat}`)
        return
      }
      if (a.action === 2 && a.seat_id === mySeat.value && a.tile >= 0) {
        let h = hand.value
        h = removeOneTile(h, a.tile)
        h = removeOneTile(h, a.tile)
        hand.value = sortHand(h)
        selectedHandIndex.value = null
        const next = [...cardsLeft.value]
        next[mySeat.value] = hand.value.length
        cardsLeft.value = next
      }
      if (a.action === 3 && a.seat_id === mySeat.value && a.tile >= 0) {
        const removeN = a.meld_kind === 5 ? 1 : a.meld_kind === 4 ? 4 : 3
        let h = hand.value
        for (let i = 0; i < removeN; ++i) h = removeOneTile(h, a.tile)
        hand.value = sortHand(h)
        selectedHandIndex.value = null
        const next = [...cardsLeft.value]
        next[mySeat.value] = hand.value.length
        cardsLeft.value = next
      }
      if (a.action === 1 && a.seat_id === mySeat.value && (a.tiles?.length || 0) >= 3) {
        let h = hand.value
        let skipDiscard = true
        for (const t of a.tiles) {
          if (skipDiscard && t === a.tile) {
            skipDiscard = false
            continue
          }
          h = removeOneTile(h, t)
        }
        hand.value = sortHand(h)
        selectedHandIndex.value = null
        const next = [...cardsLeft.value]
        next[mySeat.value] = hand.value.length
        cardsLeft.value = next
      }
      pushLog(`seat ${a.seat_id} ${text}`)
      trace('hzmj', hzmjRoundId.value, 'action', `seat=${a.seat_id} act=${a.action} kind=${a.meld_kind} tile=${a.tile} from=${a.from_seat}`)
    },
    onHzmjSettle: (s) => {
      hzmjSettle.value = s
      showHzmjSettle.value = true
      showLiuJu.value = false
      clearCountdown()
      hzmjSub.value = ''
      roomPhase.value = 'WaitReady'
      for (const e of s.entries) {
        if (e.uid === uid.value) gold.value += e.delta_gold
      }
      sock?.getLobby()
      trace('hzmj', hzmjRoundId.value, 'settle', `zimo=${s.is_zimo ? 1 : 0} hu=${s.hu_tile} M=${s.M} N=${s.N} shooter=${s.shooter_seat}`)
    },
    onHzmjLiuJu: (s) => {
      hzmjLian.value = s.lian_zhuang
      showLiuJu.value = true
      showHzmjSettle.value = false
      clearCountdown()
      hzmjSub.value = ''
      roomPhase.value = 'WaitReady'
      pushLog(`流局 连庄=${s.lian_zhuang}`)
      trace('hzmj', hzmjRoundId.value, 'liuju', `lian=${s.lian_zhuang}`)
    },
    onError: (e) => {
      let msg = e.message || `错误 ${e.code}`
      if (msg === 'action rejected' || msg.includes('非法鸣牌') || msg.includes('不能点炮胡')) {
        msg = msg.includes('不能点炮胡')
          ? msg
          : '操作无效：无牌权或不满足吃碰杠胡条件，请点「过」'
      }
      if (msg === 'discard rejected' || msg.includes('非法出牌')) msg = '出牌无效，请重选手牌'
      errorBanner.value = msg
      clearErrorSoon()
      pushLog(`err: ${msg}`)
      const game = currentGameId.value === 2 ? 'hzmj' : 'ddz'
      const roundId = game === 'hzmj' ? hzmjRoundId.value : ddzRoundId.value
      trace(game, roundId, 'error', `code=${e.code} ref=${e.ref_msg_id} ${msg}`)
      if (e.ref_msg_id === 6006 || e.ref_msg_id === 6004) {
        hzmjClaimSent.value = false
        if (hzmjSub.value === 'claim') hzmjClaimHint.value = msg
      }
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

const seatCount = computed(() => Math.max(seats.value.length, currentGameId.value === 2 ? 4 : 3))

function relSeat(offset: number) {
  if (mySeat.value < 0) return null
  const n = seatCount.value || 4
  const sid = (mySeat.value + offset) % n
  return seats.value.find((s) => s.seat_id === sid) || null
}

const seatLeft = computed(() => (currentGameId.value === 2 ? relSeat(3) : relSeat(2)))
const seatRight = computed(() => relSeat(1))
const seatOpposite = computed(() => (currentGameId.value === 2 ? relSeat(2) : null))
const seatSelf = computed(
  () => (mySeat.value < 0 ? null : seats.value.find((s) => s.seat_id === mySeat.value) || null),
)

const iAmReady = computed(() => !!seatSelf.value?.ready)

const canReady = computed(() => {
  const phase = roomPhase.value || ''
  const turn = turnPhase.value || ''
  const sub = hzmjSub.value || ''
  if (turn === 'Bid' || turn === 'Play') return false
  if (phase === 'Bid' || phase === 'Play' || phase === 'Deal') return false
  if (sub === 'discard' || sub === 'claim' || sub === 'piao') return false
  return true
})

const isHzmjClaim = computed(
  () => hzmjSub.value === 'claim' && mySeat.value >= 0 && turnSeat.value !== mySeat.value,
)
const isHzmjDiscardTurn = computed(
  () =>
    (hzmjSub.value === 'discard' || hzmjSub.value === 'piao') &&
    isMyTurn.value &&
    currentGameId.value === 2 &&
    roomPhase.value === 'Play',
)

const anGangCandidates = computed(() => listAnGangTiles(hand.value))
const buGangCandidates = computed(() =>
  listBuGangTiles(hand.value, hzmjMelds.value[mySeat.value] || []),
)

const claimTile = computed(() => hzmjLastDiscard.value?.tile ?? -1)
const claimFromSeat = computed(() => hzmjLastDiscard.value?.seat ?? -1)

const canClaimChi = computed(() =>
  canChiClaim(hand.value, claimTile.value, mySeat.value, claimFromSeat.value, seatCount.value || 4),
)
const canClaimPeng = computed(() => canPengClaim(hand.value, claimTile.value))
const canClaimGang = computed(() => canMingGangClaim(hand.value, claimTile.value))
const canClaimHu = computed(() =>
  canShowDianpaoHu(
    hzmjN.value,
    claimTile.value,
    mySeat.value,
    claimFromSeat.value,
    hzmjBanker.value,
    hzmjCaishen.value,
    hand.value,
    (hzmjMelds.value[mySeat.value] || []).length,
  ),
)
const hzmjCanZimo = ref(false)
const canZimoHu = computed(
  () =>
    isHzmjDiscardTurn.value &&
    hzmjCanZimo.value &&
    canHuHand(hand.value, (hzmjMelds.value[mySeat.value] || []).length),
)

async function checkHealth() {
  try {
    const h = await health()
    pushLog(`health: ${JSON.stringify(h.data ?? h)}`)
  } catch (e) {
    pushLog(`health failed: ${e}`)
  }
}

async function loginAndConnect(forcedDeviceId?: string) {
  busy.value = true
  errorBanner.value = ''
  try {
    const fromArg = (forcedDeviceId || '').trim().slice(0, 128)
    const deviceId =
      fromArg ||
      sessionStorage.getItem('pandora_device') ||
      `web-${Math.random().toString(16).slice(2, 10)}`
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
    pushLog(`login ok uid=${uid.value} device=${deviceId}`)

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
  currentGameId.value = resolveGameId(templateId)
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
  showHzmjSettle.value = false
  showLiuJu.value = false
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
  trace('ddz', ddzRoundId.value, 'cmd_bid', `score=${score}`)
  sock?.bid(score)
}

function toggleCard(c: number) {
  if (selected.value.includes(c)) selected.value = selected.value.filter((x) => x !== c)
  else selected.value = [...selected.value, c]
}

function selectTile(idx: number) {
  if (idx < 0 || idx >= hand.value.length) return
  selectedHandIndex.value = selectedHandIndex.value === idx ? null : idx
}

function doPlay() {
  trace('ddz', ddzRoundId.value, 'cmd_play', `cards=${selected.value.join(',')}`)
  sock?.play(false, selected.value)
}

function doPass() {
  trace('ddz', ddzRoundId.value, 'cmd_pass', '')
  sock?.play(true, [])
}

function doHzmjDiscard() {
  if (selectedHandIndex.value == null) {
    errorBanner.value = '请先点选手牌再出牌'
    clearErrorSoon()
    return
  }
  if (!sock || !wsOk.value) {
    errorBanner.value = '未连接，请等待重连'
    clearErrorSoon()
    return
  }
  const tile = hand.value[selectedHandIndex.value]
  if (tile == null) {
    errorBanner.value = '请先点选手牌再出牌'
    clearErrorSoon()
    return
  }
  pushLog(`discard ${tileLabel(tile)}`)
  trace('hzmj', hzmjRoundId.value, 'cmd_discard', `tile=${tile}`)
  sock.hzmjDiscard(tile)
}

function doHzmjAction(action: number) {
  if (!sock || !wsOk.value) {
    errorBanner.value = '未连接，请等待重连'
    clearErrorSoon()
    return
  }
  if (action === 4 && canZimoHu.value) {
    pushLog('自摸')
    trace('hzmj', hzmjRoundId.value, 'cmd_action', 'action=4 zimo')
    sock.hzmjAction(4)
    return
  }
  if (hzmjClaimSent.value) return
  if (action === 1 && !canClaimChi.value) {
    hzmjClaimHint.value = '当前不能吃（仅上家且成顺）'
    return
  }
  if (action === 2 && !canClaimPeng.value) {
    hzmjClaimHint.value = '当前不能碰'
    return
  }
  if (action === 3 && !canClaimGang.value) {
    hzmjClaimHint.value = '当前不能杠'
    return
  }
  hzmjClaimSent.value = true
  hzmjClaimHint.value = action === 0 ? '已过，等待其他玩家…' : `已提交${ACTION_LABELS[action]}，等待结果…`
  pushLog(`claim action=${ACTION_LABELS[action] || action}`)
  let chi: number[] = []
  if (action === 1) {
    const opts = listChiOptions(hand.value, claimTile.value)
    if (opts[0]) chi = [...opts[0].handTiles]
  }
  trace('hzmj', hzmjRoundId.value, 'cmd_action', `action=${action} chi=${chi.join(',')}`)
  sock.hzmjAction(action, chi)
}

function doHzmjAnGang(tile: number) {
  if (!sock || !wsOk.value) return
  trace('hzmj', hzmjRoundId.value, 'cmd_gang', `kind=0 tile=${tile}`)
  sock.hzmjGang(0, tile)
}

function doHzmjBuGang(tile: number) {
  if (!sock || !wsOk.value) return
  trace('hzmj', hzmjRoundId.value, 'cmd_gang', `kind=1 tile=${tile}`)
  sock.hzmjGang(1, tile)
}

function backLobby() {
  sock?.leaveRoom()
  showSettle.value = false
  showHzmjSettle.value = false
  showLiuJu.value = false
  settle.value = null
  hzmjSettle.value = null
  roomId.value = 0
  hand.value = []
  turnPhase.value = ''
  hzmjSub.value = ''
  clearCountdown()
  sock?.getLobby()
  router.push('/lobby')
}

function closeSettleStay() {
  showSettle.value = false
  showHzmjSettle.value = false
  showLiuJu.value = false
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
    roomTemplateId,
    currentGameId,
    seats,
    roomPhase,
    mySeat,
    hand,
    selected,
    selectedHandIndex,
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
    seatOpposite,
    seatSelf,
    hzmjCaishen,
    hzmjBanker,
    hzmjLian,
    hzmjN,
    hzmjRoundId,
    ddzRoundId,
    hzmjWall,
    hzmjSub,
    hzmjPiaoSeat,
    hzmjLastDiscard,
    hzmjMelds,
    hzmjRivers,
    hzmjSettle,
    showHzmjSettle,
    showLiuJu,
    hzmjBaseScore,
    isHzmjClaim,
    isHzmjDiscardTurn,
    anGangCandidates,
    buGangCandidates,
    canClaimChi,
    canClaimPeng,
    canClaimGang,
    canClaimHu,
    canZimoHu,
    hzmjClaimSent,
    hzmjClaimHint,
    checkHealth,
    loginAndConnect,
    refreshLobby,
    quickMatch,
    cancelMatch,
    doReady,
    doBid,
    toggleCard,
    selectTile,
    doPlay,
    doPass,
    doHzmjDiscard,
    doHzmjAction,
    doHzmjAnGang,
    doHzmjBuGang,
    backLobby,
    closeSettleStay,
    disconnect,
    applyBalances,
    cardLabel,
    tileLabel,
    pushLog,
    activityTick,
  }
}

export type { LobbyTemplate, RoomSeat }
