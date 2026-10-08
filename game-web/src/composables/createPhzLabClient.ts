import { computed, ref } from 'vue'
import { loginGuest } from '../api'
import { GameSocket } from '../net/GameSocket'
import { phzTileLabel, type LobbyTemplate, type PhzSettle, type RoomSeat } from '../net/frame'
import {
  applyPhzMeldBroadcast,
  meldKindLabel,
  pushDiscardRiver,
  restoreDiscardRiver,
  restoreReveal,
  takeClaimedFromRiver,
  type PhzMeld,
} from '../net/phzFront'
import { canChiClaim, canPengClaim } from '../net/phzMeld'
import { mingTangText } from '../net/phzMingTang'

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'

function sortHand(tiles: number[]): number[] {
  return [...tiles].sort((a, b) => a - b)
}

export function createPhzLabClient(slot: number, deviceId: string) {
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
  const phzRoundId = ref(0)
  const phzBanker = ref(0)
  const phzWall = ref(0)
  const phzSub = ref('')
  const phzCanHu = ref(false)
  const phzClaimSent = ref(false)
  const phzMelds = ref<Record<number, PhzMeld[]>>({})
  const phzRivers = ref<Record<number, number[]>>({})
  const phzReveal = ref<{ seat: number; tile: number } | null>(null)
  const phzLastDiscard = ref<{ seat: number; tile: number } | null>(null)
  const settle = ref<PhzSettle | null>(null)
  const showSettle = ref(false)
  const liujuBanker = ref(-1)

  let sock: GameSocket | null = null
  let countdownTimer: number | null = null
  let replayingSnapshot = false

  function trace(event: string, detail: string) {
    sock?.trace(phzRoundId.value, mySeat.value, 'phz', event, detail)
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
    const phase = roomPhase.value || ''
    if (phase === 'Play' || phase === 'Deal') return false
    return !!roomId.value
  })
  const isDiscardTurn = computed(() => phzSub.value === 'discard' && isMyTurn.value)
  /** Claim responders only (server sets turn_seat to discard/reveal source). */
  const isClaimTurn = computed(
    () =>
      phzSub.value === 'claim' &&
      !phzClaimSent.value &&
      mySeat.value >= 0 &&
      turnSeat.value !== mySeat.value,
  )
  const settleMingTang = computed(() => (settle.value ? mingTangText(settle.value.ming_tang_mask) : ''))
  const turnBanner = computed(() => {
    if (!phzSub.value) return ''
    if (phzSub.value === 'claim') {
      if (isClaimTurn.value) return '鸣牌'
      if (isMyTurn.value) return '等待鸣牌'
      return '座' + turnSeat.value
    }
    return isMyTurn.value ? '你的回合' : '座' + turnSeat.value
  })

  const claimTile = computed(() => {
    if (phzReveal.value) return phzReveal.value.tile
    return phzLastDiscard.value?.tile ?? -1
  })
  const claimFromSeat = computed(() => {
    if (phzReveal.value) return phzReveal.value.seat
    if (phzLastDiscard.value) return phzLastDiscard.value.seat
    return turnSeat.value
  })
  const canClaimPeng = computed(
    () => isClaimTurn.value && canPengClaim(hand.value, claimTile.value),
  )
  const canClaimChi = computed(
    () =>
      isClaimTurn.value &&
      canChiClaim(hand.value, claimTile.value, mySeat.value, claimFromSeat.value, 3),
  )

  function createSocket(): GameSocket {
    return new GameSocket({
      onLog: pushLog,
      onAuth: (ok, u) => {
        wsOk.value = ok
        if (ok) {
          if (u) uid.value = u
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
        } else {
          matching.value = false
          matchMsg.value = m.message || ''
        }
      },
      onRoomState: (rs) => {
        roomId.value = rs.room_id
        roomPhase.value = rs.phase
        seats.value = rs.seats
        const me = rs.seats.find((s) => s.uid === uid.value)
        if (me) mySeat.value = me.seat_id
      },
      onPhzGameStart: (g) => {
        const sameRound = phzRoundId.value === g.round_id && g.round_id > 0
        phzRoundId.value = g.round_id
        mySeat.value = g.self_seat
        hand.value = sortHand(g.self_hand)
        selectedHandIndex.value = null
        phzBanker.value = g.banker_seat
        phzWall.value = g.wall_remain
        replayingSnapshot = sameRound
        if (!sameRound) {
          phzMelds.value = {}
          phzRivers.value = {}
          phzReveal.value = null
          phzLastDiscard.value = null
        }
        showSettle.value = false
        settle.value = null
        liujuBanker.value = -1
        phzClaimSent.value = false
        phzSub.value = ''
        roomPhase.value = 'Play'
        pushLog(`start seat=${g.self_seat} hand=${g.self_hand.length} wall=${g.wall_remain}`)
        trace(sameRound ? 'resync' : 'start', `banker=${g.banker_seat} wall=${g.wall_remain}`)
      },
      onPhzTurn: (t) => {
        turnSeat.value = t.seat_id
        phzSub.value = t.sub
        // can_hu is per-recipient from server
        phzCanHu.value = !!t.can_hu
        phzWall.value = t.wall_remain
        startCountdown(t.timeout_s)
        // Discard turn for self: always apply authoritative hand (including empty).
        if (t.sub === 'discard' && t.seat_id === mySeat.value) {
          hand.value = sortHand(t.self_hand || [])
          selectedHandIndex.value = null
        } else if (t.self_hand && t.self_hand.length > 0) {
          hand.value = sortHand(t.self_hand)
          selectedHandIndex.value = null
        }
        phzClaimSent.value = false
        replayingSnapshot = false
        trace('turn', `seat=${t.seat_id} sub=${t.sub} left=${t.timeout_s} can_hu=${t.can_hu ? 1 : 0}`)
      },
      onPhzDraw: (d) => {
        if (d.seat_id === mySeat.value && d.tile >= 0) {
          pushLog(`draw visible ${phzTileLabel(d.tile)}`)
        } else {
          pushLog(`seat${d.seat_id} draw`)
        }
        if (phzWall.value > 0) phzWall.value -= 1
        trace('draw', `seat=${d.seat_id} tile=${d.tile}`)
      },
      onPhzReveal: (d) => {
        phzReveal.value = replayingSnapshot
          ? restoreReveal(phzReveal.value, d.seat_id, d.tile)
          : { seat: d.seat_id, tile: d.tile }
        phzLastDiscard.value = null
        pushLog(`reveal seat${d.seat_id} ${phzTileLabel(d.tile)}`)
        trace('reveal', `seat=${d.seat_id} tile=${d.tile}`)
      },
      onPhzDiscard: (d) => {
        phzLastDiscard.value = { seat: d.seat_id, tile: d.tile }
        phzReveal.value = null
        const rivers = { ...phzRivers.value }
        const prev = rivers[d.seat_id] || []
        rivers[d.seat_id] = replayingSnapshot
          ? restoreDiscardRiver(prev, d.tile)
          : pushDiscardRiver(prev, d.tile)
        phzRivers.value = rivers
        if (!replayingSnapshot && d.seat_id === mySeat.value) {
          const i = hand.value.indexOf(d.tile)
          if (i >= 0) {
            const next = [...hand.value]
            next.splice(i, 1)
            hand.value = next
          }
          selectedHandIndex.value = null
          phzSub.value = ''
        }
        pushLog(`seat${d.seat_id} discard ${phzTileLabel(d.tile)}`)
        trace('discard', `seat=${d.seat_id} tile=${d.tile}`)
      },
      onPhzAction: (a) => {
        phzClaimSent.value = false
        const kind = a.meld_kind || a.action
        const lab = meldKindLabel(kind) || `act${a.action}`
        const melds = { ...phzMelds.value }
        melds[a.seat_id] = applyPhzMeldBroadcast(
          melds[a.seat_id] || [],
          a.action,
          a.tile,
          a.tiles || [],
          a.meld_kind || 0,
          a.from_seat ?? -1,
        )
        phzMelds.value = melds
        // Drop used hand tiles for self chi/peng (server sync may arrive next as empty).
        if (!replayingSnapshot && a.seat_id === mySeat.value && (kind === 1 || kind === 2)) {
          let next = [...hand.value]
          const faces = a.tiles && a.tiles.length ? a.tiles : []
          if (kind === 2) {
            // peng: remove two of claim tile from hand
            for (let n = 0; n < 2; n++) {
              const i = next.indexOf(a.tile)
              if (i >= 0) next.splice(i, 1)
            }
          } else if (faces.length >= 3) {
            // chi: remove the two non-claim faces
            let claimLeft = 1
            for (const t of faces) {
              if (t === a.tile && claimLeft > 0) {
                claimLeft--
                continue
              }
              const i = next.indexOf(t)
              if (i >= 0) next.splice(i, 1)
            }
          }
          hand.value = sortHand(next)
          selectedHandIndex.value = null
        }
        if (!replayingSnapshot && a.from_seat >= 0 && a.tile >= 0 && kind !== 3 && kind !== 4 && kind !== 6) {
          const rivers = { ...phzRivers.value }
          rivers[a.from_seat] = takeClaimedFromRiver(rivers[a.from_seat] || [], a.tile)
          phzRivers.value = rivers
          if (phzReveal.value && phzReveal.value.tile === a.tile) phzReveal.value = null
        }
        pushLog(`seat${a.seat_id} ${lab} ${a.tile >= 0 ? phzTileLabel(a.tile) : ''}`)
        trace('action', `seat=${a.seat_id} kind=${kind} tile=${a.tile}`)
      },
      onPhzSettle: (s) => {
        settle.value = s
        showSettle.value = true
        clearCountdown()
        phzSub.value = ''
        roomPhase.value = 'WaitReady'
        for (const e of s.entries) {
          if (e.uid === uid.value) gold.value += e.delta_gold
        }
        sock?.getLobby()
        trace('settle', `xi=${s.hu_xi} tun=${s.tun} fan=${s.fan}`)
      },
      onPhzLiuJu: (s) => {
        liujuBanker.value = s.banker_seat
        showSettle.value = true
        settle.value = null
        phzSub.value = ''
        roomPhase.value = 'WaitReady'
        pushLog(`liuju banker=${s.banker_seat}`)
        trace('liuju', `banker=${s.banker_seat}`)
      },
      onError: (e) => {
        errorBanner.value = e.message || `err ${e.code}`
        clearErrorSoon()
        // Unlock claim/discard UI after rejected actions
        if (e.ref_msg_id === 7007 || e.ref_msg_id === 7005 || phzSub.value === 'claim') {
          phzClaimSent.value = false
        }
        pushLog(`err: ${e.message}`)
      },
    })
  }

  function connectWithToken(accessToken: string, profile?: { uid: number; nickname: string; gold: number; diamond: number }) {
    token.value = accessToken
    if (profile) {
      uid.value = profile.uid
      nickname.value = profile.nickname
      gold.value = profile.gold
      diamond.value = profile.diamond
    }
    sock?.close()
    sock = createSocket()
    sock.connect(WSS_URL, token.value)
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
      connectWithToken(r.data.access_token, {
        uid: r.data.uid,
        nickname: r.data.nickname,
        gold: r.data.gold,
        diamond: r.data.diamond,
      })
      pushLog(`login ok ${nickname.value}`)
    } catch (e) {
      errorBanner.value = String(e)
    } finally {
      busy.value = false
    }
  }

  function phzTemplateId(): number {
    const t = templates.value.find((x) => x.game_id === 3 && x.enabled)
    return t?.id ?? 3
  }

  function matchPhz() {
    const tid = phzTemplateId()
    matching.value = true
    sock?.quickMatch(tid)
    pushLog(`match template=${tid}`)
  }

  function doReady() {
    sock?.ready(true)
  }

  function selectTile(i: number) {
    selectedHandIndex.value = selectedHandIndex.value === i ? null : i
  }

  function discardSelected() {
    if (selectedHandIndex.value == null) return
    const tile = hand.value[selectedHandIndex.value]
    if (tile == null) return
    sock?.phzDiscard(tile)
  }

  function claim(action: number) {
    if (phzClaimSent.value) return
    if (action === 1 && !canClaimChi.value) return
    if (action === 2 && !canClaimPeng.value) return
    if (action === 4 && !phzCanHu.value) return
    phzClaimSent.value = true
    sock?.phzAction(action, [])
  }

  function dismissSettle() {
    showSettle.value = false
  }

  return {
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
    phzRoundId,
    phzBanker,
    phzWall,
    phzSub,
    phzCanHu,
    phzClaimSent,
    phzMelds,
    phzRivers,
    phzReveal,
    phzLastDiscard,
    settle,
    showSettle,
    liujuBanker,
    settleMingTang,
    turnBanner,
    claimTile,
    canClaimChi,
    canClaimPeng,
    isMyTurn,
    iAmReady,
    canReady,
    isDiscardTurn,
    isClaimTurn,
    login,
    connectWithToken,
    matchPhz,
    doReady,
    selectTile,
    discardSelected,
    claim,
    dismissSettle,
    phzTileLabel,
  }
}
