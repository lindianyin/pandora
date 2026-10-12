import { computed, ref, watch } from 'vue'
import { labTopupGold, loginGuest } from '../api'
import { GameSocket } from '../net/GameSocket'
import {
  BIJI_DUN_TYPE_LABELS,
  SSS_FIVE_TYPE_LABELS,
  sssCardFace,
  type LobbyTemplate,
  type RoomSeat,
} from '../net/frame'
import {
  autoArrangeHand,
  detectSpecial,
  isLegalArrange,
  specialLabel,
  SSS_SPECIAL_LABELS,
} from '../net/sssArrange'

export { autoArrangeHand, detectSpecial, isLegalArrange, specialLabel }

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'
const TEMPLATE_ID = 6

const HEAD_N = 3
const MID_N = 5
const TAIL_N = 5

function rankOf(c: number) {
  return c % 13
}

function sortHand(cards: number[], highFirst: boolean) {
  return cards.slice().sort((a, b) => {
    const ra = rankOf(a)
    const rb = rankOf(b)
    if (ra !== rb) return highFirst ? rb - ra : ra - rb
    return highFirst ? b - a : a - b
  })
}

export function createSssLabClient(slot: number, deviceId: string) {
  const label = `P${slot + 1}`
  const nickname = ref('')
  const uid = ref(0)
  const gold = ref(0)
  const token = ref('')
  const wsOk = ref(false)
  const busy = ref(false)
  const logs = ref<string[]>([])
  const errorBanner = ref('')
  const templates = ref<LobbyTemplate[]>([])
  const matching = ref(false)
  const roomId = ref(0)
  const roomPhase = ref('')
  const seats = ref<RoomSeat[]>([])
  const mySeat = ref(-1)
  const roundId = ref(0)
  const hand = ref<number[]>([])
  const pool = ref<number[]>([])
  const head = ref<number[]>([])
  const mid = ref<number[]>([])
  const tail = ref<number[]>([])
  const selected = ref<number | null>(null)
  const locked = ref(false)
  const remainS = ref(0)
  const settleText = ref('')
  const compareText = ref('')
  const reportSpecial = ref(false)
  const enableSpecial = ref(false)
  let sock: GameSocket | null = null
  let countdownTimer: number | null = null

  const canMatch = computed(() => wsOk.value && !roomId.value && !matching.value)
  const canReady = computed(() => !!roomId.value && roomPhase.value === 'WaitReady')
  const arranging = computed(
    () => !!roundId.value && !settleText.value && hand.value.length === 13 && !locked.value,
  )
  const poolFaces = computed(() => pool.value.map(sssCardFace))
  const headFaces = computed(() => head.value.map(sssCardFace))
  const midFaces = computed(() => mid.value.map(sssCardFace))
  const tailFaces = computed(() => tail.value.map(sssCardFace))
  const complete = computed(
    () => head.value.length === HEAD_N && mid.value.length === MID_N && tail.value.length === TAIL_N,
  )
  const canConfirm = computed(() => arranging.value && complete.value)
  const detectedSpecial = computed(() =>
    hand.value.length === 13 ? detectSpecial(hand.value, enableSpecial.value) : 0,
  )
  const canReportSpecial = computed(() => arranging.value && detectedSpecial.value > 0)
  const specialTypeLabel = computed(() => specialLabel(detectedSpecial.value))

  const arrangeHint = computed(() => {
    if (!arranging.value) return ''
    if (reportSpecial.value && detectedSpecial.value > 0) {
      return `特殊报到：${specialTypeLabel.value}（无需普通墩序）`
    }
    if (!complete.value) {
      const left = pool.value.length
      return `请把头3/中5/尾5摆满（剩余 ${left} 张）`
    }
    if (!isLegalArrange(head.value, mid.value, tail.value)) return '当前摆法倒水，请调整或点智能摆牌'
    return '摆满后可确认'
  })

  const canConfirmLegal = computed(() => {
    if (!arranging.value) return false
    if (reportSpecial.value && detectedSpecial.value > 0) return true
    return complete.value && isLegalArrange(head.value, mid.value, tail.value)
  })

  watch(canReportSpecial, (ok) => {
    if (!ok) reportSpecial.value = false
  })

  function resetBoardFromHand(cards: number[]) {
    hand.value = cards.slice()
    pool.value = sortHand(cards, true)
    head.value = []
    mid.value = []
    tail.value = []
    selected.value = null
    reportSpecial.value = false
  }

  function ensurePartitionPayload() {
    if (complete.value) return
    const sorted = sortHand(hand.value, false)
    head.value = sorted.slice(0, HEAD_N)
    mid.value = sorted.slice(HEAD_N, HEAD_N + MID_N)
    tail.value = sorted.slice(HEAD_N + MID_N, HEAD_N + MID_N + TAIL_N)
    pool.value = []
    selected.value = null
  }

  function pushLog(line: string) {
    const t = new Date().toLocaleTimeString()
    logs.value = [`[${t}] ${line}`, ...logs.value].slice(0, 40)
  }

  function stopCountdown() {
    if (countdownTimer != null) {
      clearInterval(countdownTimer)
      countdownTimer = null
    }
  }

  function startCountdown(seconds: number) {
    stopCountdown()
    remainS.value = Math.max(0, Math.floor(seconds))
    if (remainS.value <= 0) return
    countdownTimer = window.setInterval(() => {
      if (remainS.value <= 0) {
        stopCountdown()
        return
      }
      remainS.value -= 1
    }, 1000)
  }

  function applyAutoArrange() {
    if (!hand.value.length) return
    const arr = autoArrangeHand(hand.value)
    head.value = arr.head
    mid.value = arr.mid
    tail.value = arr.tail
    pool.value = []
    selected.value = null
  }

  function sendConfirm() {
    sock?.sssArrange(head.value, mid.value, tail.value, reportSpecial.value, true)
  }

  async function login() {
    busy.value = true
    try {
      const r = await loginGuest(deviceId)
      if (r.code !== 0 || !r.data?.access_token) throw new Error(r.message || 'login failed')
      token.value = r.data.access_token
      uid.value = r.data.uid
      nickname.value = r.data.nickname || label
      gold.value = r.data.gold
      pushLog(`login uid=${r.data.uid}`)
      connect()
    } catch (e: unknown) {
      errorBanner.value = String(e)
    } finally {
      busy.value = false
    }
  }

  function connect() {
    if (!token.value) return
    sock?.close()
    sock = new GameSocket({
      onLog: pushLog,
      onAuth: (ok) => {
        wsOk.value = ok
        if (ok) sock?.getLobby()
      },
      onLobby: (info) => {
        templates.value = info.templates
        gold.value = info.gold
      },
      onMatchStatus: (s) => {
        matching.value = s.status === 0
        if (s.room_id) roomId.value = s.room_id
      },
      onRoomState: (s) => {
        roomId.value = s.room_id
        roomPhase.value = s.phase
        seats.value = s.seats
        const me = s.seats.find((x) => x.uid === uid.value)
        if (me) mySeat.value = me.seat_id
      },
      onSssGameStart: (s) => {
        roundId.value = s.round_id
        mySeat.value = s.self_seat
        locked.value = false
        settleText.value = ''
        compareText.value = ''
        roomPhase.value = 'Arrange'
        enableSpecial.value = s.enable_special
        if (!s.enable_special) reportSpecial.value = false
        resetBoardFromHand(s.hand)
        startCountdown(s.arrange_timeout_s > 0 ? s.arrange_timeout_s : 60)
        pushLog(`GameStart timeout=${s.arrange_timeout_s}s cards=${s.hand.length}`)
      },
      onSssArrangeState: (s) => {
        if (s.remain_s > 0) startCountdown(s.remain_s)
        else remainS.value = 0
        if (mySeat.value >= 0 && s.locked[mySeat.value]) {
          locked.value = true
          stopCountdown()
        }
      },
      onSssArrangeAck: (s) => {
        if (s.locked) {
          locked.value = true
          stopCountdown()
        }
        pushLog(`ArrangeAck code=${s.code} ${s.message}`)
        if (s.code !== 0) {
          errorBanner.value = s.message || `摆牌失败 code=${s.code}`
        }
      },
      onSssCompare: (s) => {
        const parts = s.seats.map((seat) => {
          const sp = seat.special_type ?? 0
          if (sp > 0) {
            const name = SSS_SPECIAL_LABELS[sp] || `special${sp}`
            return `s${seat.seat_id}:特殊[${name}]`
          }
          const ht = seat.head?.type ?? 0
          const mt = seat.mid?.type ?? 0
          const tt = seat.tail?.type ?? 0
          const hLabel =
            seat.head?.cards.length === 3
              ? BIJI_DUN_TYPE_LABELS[ht] ?? `t${ht}`
              : `t${ht}`
          const mLabel = SSS_FIVE_TYPE_LABELS[mt] ?? `t${mt}`
          const tLabel = SSS_FIVE_TYPE_LABELS[tt] ?? `t${tt}`
          return `s${seat.seat_id}:${hLabel}/${mLabel}/${tLabel}`
        })
        compareText.value = parts.join(' · ')
        if (s.shoots?.length) {
          compareText.value += ` · 打枪×${s.shoots.length}`
        }
        pushLog(`Compare ${compareText.value}`)
      },
      onSssSettle: (s) => {
        stopCountdown()
        remainS.value = 0
        const me = s.seats.find((x) => x.uid === uid.value)
        const spName =
          me && me.special_type > 0 ? SSS_SPECIAL_LABELS[me.special_type] || `sp${me.special_type}` : ''
        settleText.value = me
          ? `水=${me.gross_water} net=${me.net} gold=${me.gold}${spName ? ` 特殊[${spName}]` : ''}${
              me.home_run ? ' 全垒打' : ''
            }`
          : 'settled'
        gold.value = me?.gold ?? gold.value
        roomPhase.value = 'WaitReady'
        pushLog(`Settle ${settleText.value}`)
      },
      onSssSnapshot: (s) => {
        locked.value = s.locked
        reportSpecial.value = s.report_special
        if (s.has_draft && s.draft_head.length === HEAD_N) {
          hand.value = [...s.hand]
          head.value = [...s.draft_head]
          mid.value = [...s.draft_mid]
          tail.value = [...s.draft_tail]
          pool.value = []
          selected.value = null
        } else if (s.hand.length) {
          resetBoardFromHand([...s.hand])
        }
        if (s.locked) stopCountdown()
        else if (s.remain_s > 0) startCountdown(s.remain_s)
        else remainS.value = s.remain_s
      },
      onError: (e) => {
        errorBanner.value = `${e.code}: ${e.message}`
      },
    })
    sock.connect(WSS_URL, token.value)
  }

  function oneClickMatch() {
    sock?.quickMatch(TEMPLATE_ID)
    matching.value = true
  }
  function oneClickReady() {
    sock?.ready(true)
  }
  function oneClickLeave() {
    stopCountdown()
    sock?.leaveRoom()
    roomId.value = 0
    roundId.value = 0
    matching.value = false
    remainS.value = 0
    selected.value = null
    compareText.value = ''
  }

  function toggleSelect(cardId: number) {
    if (locked.value || !arranging.value) return
    if (!pool.value.includes(cardId)) return
    selected.value = selected.value === cardId ? null : cardId
  }

  function maxFor(zone: 'head' | 'mid' | 'tail') {
    if (zone === 'head') return HEAD_N
    if (zone === 'mid') return MID_N
    return TAIL_N
  }

  function placeTo(zone: 'head' | 'mid' | 'tail') {
    if (locked.value || selected.value == null) return
    const target = zone === 'head' ? head : zone === 'mid' ? mid : tail
    if (target.value.length >= maxFor(zone)) return
    const card = selected.value
    const idx = pool.value.indexOf(card)
    if (idx < 0) return
    pool.value = pool.value.filter((_, i) => i !== idx)
    target.value = [...target.value, card]
    selected.value = null
  }

  function takeFrom(zone: 'head' | 'mid' | 'tail', cardId: number) {
    if (locked.value) return
    const target = zone === 'head' ? head : zone === 'mid' ? mid : tail
    const idx = target.value.indexOf(cardId)
    if (idx < 0) return
    target.value = target.value.filter((_, i) => i !== idx)
    pool.value = sortHand([...pool.value, cardId], true)
    selected.value = null
  }

  function clearBoard() {
    if (locked.value || !hand.value.length) return
    resetBoardFromHand(hand.value)
  }

  function smartFill() {
    if (locked.value || !hand.value.length) return
    applyAutoArrange()
  }

  function confirmArrange() {
    if (!canConfirmLegal.value) return
    if (reportSpecial.value && detectedSpecial.value > 0) {
      ensurePartitionPayload()
      sendConfirm()
      return
    }
    sendConfirm()
  }

  function oneClickConfirm() {
    if (reportSpecial.value && detectedSpecial.value > 0) {
      ensurePartitionPayload()
      sendConfirm()
      return
    }
    applyAutoArrange()
    if (!isLegalArrange(head.value, mid.value, tail.value)) {
      errorBanner.value = '智能摆牌未得到合法墩序'
      return
    }
    reportSpecial.value = false
    sendConfirm()
  }

  function autoConfirm() {
    oneClickConfirm()
  }

  async function topupGold() {
    if (!token.value) return
    const r = await labTopupGold(token.value, 100000)
    if (r.code === 0 && r.data?.gold != null) gold.value = r.data.gold
  }

  return {
    label,
    nickname,
    uid,
    gold,
    wsOk,
    busy,
    logs,
    errorBanner,
    templates,
    matching,
    roomId,
    roomPhase,
    seats,
    mySeat,
    roundId,
    hand,
    pool,
    head,
    mid,
    tail,
    selected,
    locked,
    remainS,
    settleText,
    compareText,
    reportSpecial,
    enableSpecial,
    detectedSpecial,
    canReportSpecial,
    specialTypeLabel,
    poolFaces,
    headFaces,
    midFaces,
    tailFaces,
    complete,
    canConfirm,
    canConfirmLegal,
    arrangeHint,
    arranging,
    canMatch,
    canReady,
    login,
    oneClickMatch,
    oneClickReady,
    oneClickLeave,
    toggleSelect,
    placeTo,
    takeFrom,
    clearBoard,
    smartFill,
    confirmArrange,
    oneClickConfirm,
    autoConfirm,
    topupGold,
    HEAD_N,
    MID_N,
    TAIL_N,
  }
}
