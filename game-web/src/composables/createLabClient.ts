import { computed, ref, type Ref } from 'vue'
import { loginGuest } from '../api'
import { GameSocket } from '../net/GameSocket'
import { tileLabel, type LobbyTemplate, type RoomSeat, type HzmjSettle } from '../net/frame'
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

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'
const ACTION_LABELS = ['\u8fc7', '\u5403', '\u78b0', '\u6760', '\u80e1']

export type LabClient = ReturnType<typeof createLabClient>

export function createLabClient(slot: number, deviceId: string) {
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
  const selectedHandIndex = ref<number | null>(null)
  const turnSeat = ref(-1)
  const countdown = ref(0)
  const cardsLeft = ref<number[]>([0, 0, 0, 0])
  const lastPlays = ref<Record<number, string>>({})

  const hzmjCaishen = ref<number[]>([33])
  const hzmjBanker = ref(0)
  const hzmjN = ref(2)
  const hzmjWall = ref(0)
  const hzmjSub = ref('')
  const hzmjLastDiscard = ref<{ seat: number; tile: number } | null>(null)
  const hzmjMelds = ref<Record<number, HzmjMeld[]>>({})
  const hzmjRivers = ref<Record<number, number[]>>({})
  const hzmjSettle = ref<HzmjSettle | null>(null)
  const showSettle = ref(false)
  const hzmjClaimSent = ref(false)
  const hzmjClaimHint = ref('')

  let sock: GameSocket | null = null
  let countdownTimer: number | null = null
  let replayingSnapshot = false

  function trace(event: string, detail: string) {
    sock?.trace(hzmjRoundId.value, mySeat.value, 'hzmj', event, detail)
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
  const isHzmjClaim = computed(
    () => hzmjSub.value === 'claim' && mySeat.value >= 0 && turnSeat.value !== mySeat.value,
  )
  const isHzmjDiscardTurn = computed(
    () =>
      (hzmjSub.value === 'discard' || hzmjSub.value === 'piao') &&
      isMyTurn.value &&
      roomPhase.value === 'Play',
  )
  const anGangCandidates = computed(() => listAnGangTiles(hand.value))
  const buGangCandidates = computed(() => listBuGangTiles(hand.value, hzmjMelds.value[mySeat.value] || []))
  const hzmjRoundId = ref(0)
  const claimTile = computed(() => hzmjLastDiscard.value?.tile ?? -1)
  const claimFromSeat = computed(() => hzmjLastDiscard.value?.seat ?? -1)
  const canClaimChi = computed(() =>
    canChiClaim(hand.value, claimTile.value, mySeat.value, claimFromSeat.value, 4),
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
  const canReady = computed(() => {
    const phase = roomPhase.value || ''
    const sub = hzmjSub.value || ''
    if (phase === 'Play' || phase === 'Deal') return false
    if (sub === 'discard' || sub === 'claim' || sub === 'piao') return false
    return !!roomId.value
  })

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
      onHzmjGameStart: (g) => {
        const sameRound = hzmjRoundId.value !== 0 && g.round_id === hzmjRoundId.value
        hzmjRoundId.value = g.round_id
        roomId.value = g.room_id || roomId.value
        mySeat.value = g.self_seat
        hand.value = sortHand(g.self_hand)
        selectedHandIndex.value = null
        hzmjCaishen.value = g.caishen.length ? [...g.caishen] : [33]
        hzmjBanker.value = g.banker_seat
        hzmjN.value = g.N
        hzmjWall.value = g.wall_remain
        hzmjMelds.value = {}
        replayingSnapshot = sameRound
        if (!sameRound) {
          hzmjRivers.value = {}
          hzmjLastDiscard.value = null
          lastPlays.value = {}
        }
        showSettle.value = false
        hzmjSettle.value = null
        hzmjClaimSent.value = false
        hzmjSub.value = ''
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
        pushLog(`start seat=${g.self_seat} N=${g.N} hand=${g.self_hand.length}`)
        trace(sameRound ? 'resync' : 'start', `banker=${g.banker_seat} N=${g.N} wall=${g.wall_remain} hand=${g.self_hand.join(',')}`)
      },
      onHzmjTurn: (t) => {
        turnSeat.value = t.seat_id
        hzmjSub.value = t.sub
        hzmjCanZimo.value = !!t.can_zimo && t.seat_id === mySeat.value
        hzmjWall.value = t.wall_remain
        startCountdown(t.timeout_s)
        if (t.self_hand && t.self_hand.length > 0) {
          hand.value = sortHand(t.self_hand)
          selectedHandIndex.value = null
          const next = [...cardsLeft.value]
          next[mySeat.value] = t.self_hand.length
          cardsLeft.value = next
        }
        if (t.sub === 'claim') {
          hzmjClaimSent.value = false
          hzmjClaimHint.value = '\u8bf7\u9e23\u724c\u6216\u8fc7'
        } else {
          hzmjClaimSent.value = false
          hzmjClaimHint.value = ''
          selectedHandIndex.value = null
        }
        replayingSnapshot = false
        trace('turn', `seat=${t.seat_id} sub=${t.sub} timeout=${t.timeout_s} wall=${t.wall_remain} hand=${(t.self_hand || []).join(',')}`)
      },
      onHzmjDraw: (d) => {
        if (d.seat_id === mySeat.value && d.tile >= 0) {
          hand.value = sortHand([...hand.value, d.tile])
          selectedHandIndex.value = hand.value.lastIndexOf(d.tile)
          pushLog(`摸 ${tileLabel(d.tile)}`)
        } else if (d.seat_id !== mySeat.value) {
          pushLog(`seat${d.seat_id} 摸牌`)
        }
        const next = [...cardsLeft.value]
        if (typeof next[d.seat_id] === 'number') next[d.seat_id]! += 1
        cardsLeft.value = next
        if (hzmjWall.value > 0) hzmjWall.value -= 1
        lastPlays.value = { ...lastPlays.value, [d.seat_id]: '\u6478\u724c' }
        trace('draw', `seat=${d.seat_id} tile=${d.tile}`)
      },
      onHzmjDiscard: (d) => {
        hzmjLastDiscard.value = { seat: d.seat_id, tile: d.tile }
        lastPlays.value = { ...lastPlays.value, [d.seat_id]: tileLabel(d.tile) }
        const rivers = { ...hzmjRivers.value }
        const prev = rivers[d.seat_id] || []
        rivers[d.seat_id] = replayingSnapshot
          ? restoreDiscardRiver(prev, d.tile)
          : pushDiscardRiver(prev, d.tile)
        hzmjRivers.value = rivers
        if (replayingSnapshot) {
          pushLog(`seat${d.seat_id} discard ${tileLabel(d.tile)}`)
          trace('discard', `seat=${d.seat_id} tile=${d.tile}`)
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
        pushLog(`seat${d.seat_id} discard ${tileLabel(d.tile)}`)
        trace('discard', `seat=${d.seat_id} tile=${d.tile}`)
      },
      onHzmjAction: (a) => {
        hzmjClaimSent.value = false
        hzmjClaimHint.value = ''
        const kind = a.meld_kind || a.action
        const lab = meldKindLabel(kind) || ACTION_LABELS[a.action] || `act${a.action}`
        const text = a.tile >= 0 ? `${lab} ${tileLabel(a.tile)}` : lab
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
          pushLog(`seat${a.seat_id} ${text}`)
          trace('action', `seat=${a.seat_id} act=${a.action} kind=${a.meld_kind} tile=${a.tile} from=${a.from_seat}`)
          return
        }
        if (a.seat_id === mySeat.value && a.action >= 1 && a.action <= 3) {
          selectedHandIndex.value = null
        }
        pushLog(`seat${a.seat_id} ${text}`)
        trace('action', `seat=${a.seat_id} act=${a.action} kind=${a.meld_kind} tile=${a.tile} from=${a.from_seat}`)
      },
      onHzmjSettle: (s) => {
        hzmjSettle.value = s
        showSettle.value = true
        clearCountdown()
        hzmjSub.value = ''
        roomPhase.value = 'WaitReady'
        for (const e of s.entries) {
          if (e.uid === uid.value) gold.value += e.delta_gold
        }
        sock?.getLobby()
        trace('settle', `zimo=${s.is_zimo ? 1 : 0} hu=${s.hu_tile} M=${s.M} N=${s.N} shooter=${s.shooter_seat}`)
      },
      onHzmjLiuJu: (s) => {
        showSettle.value = true
        hzmjSub.value = ''
        roomPhase.value = 'WaitReady'
        pushLog(`liuju lian=${s.lian_zhuang}`)
        trace('liuju', `lian=${s.lian_zhuang}`)
      },
      onError: (e) => {
        let msg = e.message || `err ${e.code}`
        if (msg.includes('\u975e\u6cd5\u9e23\u724c') || msg.includes('\u4e0d\u80fd\u70b9\u70ae')) {
          /* keep server msg */
        }
        errorBanner.value = msg
        clearErrorSoon()
        pushLog(`err: ${msg}`)
        if (e.ref_msg_id === 6006) hzmjClaimSent.value = false
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

  function hzmjTemplateId(): number {
    const t = templates.value.find((x) => x.game_id === 2 && x.enabled)
    return t?.id ?? 2
  }

  function matchHzmj() {
    matching.value = true
    matchMsg.value = 'matching'
    sock?.quickMatch(hzmjTemplateId())
  }

  function doReady() {
    showSettle.value = false
    sock?.ready(true)
  }

  function selectTile(idx: number) {
    if (idx < 0 || idx >= hand.value.length) return
    selectedHandIndex.value = selectedHandIndex.value === idx ? null : idx
  }

  function doDiscard() {
    if (selectedHandIndex.value == null || !sock) return
    const tile = hand.value[selectedHandIndex.value]
    if (tile == null) return
    trace('cmd_discard', `tile=${tile}`)
    sock.hzmjDiscard(tile)
  }

  function doAction(action: number) {
    if (!sock) return
    if (action === 4 && canZimoHu.value && isHzmjDiscardTurn.value) {
      trace('cmd_action', 'action=4 zimo')
      sock.hzmjAction(4)
      pushLog('\u81ea\u6478')
      return
    }
    if (hzmjClaimSent.value) return
    let chi: number[] = []
    if (action === 1) {
      const opts = listChiOptions(hand.value, claimTile.value)
      if (opts[0]) chi = [...opts[0].handTiles]
    }
    hzmjClaimSent.value = true
    trace('cmd_action', `action=${action} chi=${chi.join(',')}`)
    sock.hzmjAction(action, chi)
  }

  function doAnGang(tile: number) {
    trace('cmd_gang', `kind=0 tile=${tile}`)
    sock?.hzmjGang(0, tile)
  }

  function doBuGang(tile: number) {
    trace('cmd_gang', `kind=1 tile=${tile}`)
    sock?.hzmjGang(1, tile)
  }

  function leave() {
    sock?.leaveRoom()
    roomId.value = 0
    hzmjSub.value = ''
    hand.value = []
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
    selectedHandIndex,
    turnSeat,
    countdown,
    cardsLeft,
    lastPlays,
    hzmjCaishen,
    hzmjBanker,
    hzmjN,
    hzmjWall,
    hzmjRoundId,
    hzmjSub,
    hzmjLastDiscard,
    hzmjMelds,
    hzmjRivers,
    hzmjSettle,
    showSettle,
    hzmjClaimSent,
    hzmjClaimHint,
    isMyTurn,
    iAmReady,
    canReady,
    isHzmjClaim,
    isHzmjDiscardTurn,
    anGangCandidates,
    buGangCandidates,
    doBuGang,
    canClaimChi,
    canClaimPeng,
    canClaimGang,
    canClaimHu,
    canZimoHu,
    login,
    matchHzmj,
    doReady,
    selectTile,
    doDiscard,
    doAction,
    doAnGang,
    leave,
    dispose,
    tileLabel,
    meldKindLabel,
  }
}

export type LabClientRefs = {
  [K in keyof LabClient]: LabClient[K] extends Ref<infer V> ? V : LabClient[K]
}
