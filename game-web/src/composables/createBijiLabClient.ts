import { computed, ref } from 'vue'
import { labTopupGold, loginGuest } from '../api'
import { GameSocket } from '../net/GameSocket'
import {
  BIJI_DUN_TYPE_LABELS,
  bijiCardFace,
  type LobbyTemplate,
  type RoomSeat,
} from '../net/frame'

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'
const TEMPLATE_ID = 5

function rankOf(c: number) {
  return c % 13
}
function suitOf(c: number) {
  return Math.floor(c / 13)
}
function evalDun(cards: number[]) {
  const c = cards.slice().sort((a, b) => {
    if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
    return suitOf(b) - suitOf(a)
  })
  const r = c.map(rankOf)
  const s = c.map(suitOf)
  const sameSuit = s[0] === s[1] && s[1] === s[2]
  const sortedR = [...r].sort((a, b) => a - b)
  let stTop: number | null = null
  if (sortedR[0] === 0 && sortedR[1] === 1 && sortedR[2] === 12) stTop = -1
  else if (sortedR[1] === sortedR[0]! + 1 && sortedR[2] === sortedR[1]! + 1) stTop = sortedR[2]!
  if (r[0] === r[1] && r[1] === r[2]) return [5, r[0]!, 0, 0, Math.max(...s), 0, 0]
  if (sameSuit && stTop !== null) return [4, stTop, 0, 0, Math.max(...s), 0, 0]
  if (sameSuit) return [3, r[0]!, r[1]!, r[2]!, s[0]!, s[1]!, s[2]!]
  if (stTop !== null) return [2, stTop, 0, 0, Math.max(...s), 0, 0]
  if (r[0] === r[1] || r[1] === r[2] || r[0] === r[2]) {
    let pr = 0,
      kr = 0,
      ps = 0,
      ks = 0
    if (r[0] === r[1]) {
      pr = r[0]!
      kr = r[2]!
      ps = Math.max(s[0]!, s[1]!)
      ks = s[2]!
    } else if (r[1] === r[2]) {
      pr = r[1]!
      kr = r[0]!
      ps = Math.max(s[1]!, s[2]!)
      ks = s[0]!
    } else {
      pr = r[0]!
      kr = r[1]!
      ps = Math.max(s[0]!, s[2]!)
      ks = s[1]!
    }
    return [1, pr, kr, 0, ps, ks, 0]
  }
  return [0, r[0]!, r[1]!, r[2]!, s[0]!, s[1]!, s[2]!]
}
function cmpDun(a: number[], b: number[]) {
  const ka = evalDun(a)
  const kb = evalDun(b)
  for (let i = 0; i < ka.length; i++) if (ka[i] !== kb[i]) return ka[i]! < kb[i]! ? -1 : 1
  return 0
}
export function isLegalArrange(h: number[], m: number[], t: number[]) {
  if (h.length !== 3 || m.length !== 3 || t.length !== 3) return false
  return cmpDun(h, m) <= 0 && cmpDun(m, t) <= 0
}

export function dunTypeLabel(cards: number[]) {
  if (cards.length !== 3) return ''
  const t = evalDun(cards)[0] ?? 0
  return BIJI_DUN_TYPE_LABELS[t] ?? ''
}

export function autoArrangeHand(hand: number[]) {
  let best: { head: number[]; mid: number[]; tail: number[] } | null = null
  let bestKey: number[][] | null = null
  for (let i = 0; i < 9; i++)
    for (let j = i + 1; j < 9; j++)
      for (let k = j + 1; k < 9; k++) {
        const head = [hand[i]!, hand[j]!, hand[k]!]
        const rem: number[] = []
        for (let t = 0; t < 9; t++) if (t !== i && t !== j && t !== k) rem.push(t)
        for (let a = 0; a < 6; a++)
          for (let b = a + 1; b < 6; b++)
            for (let c = b + 1; c < 6; c++) {
              const mid = [hand[rem[a]!]!, hand[rem[b]!]!, hand[rem[c]!]!]
              const tailIdx: number[] = []
              for (let t = 0; t < 6; t++) if (t !== a && t !== b && t !== c) tailIdx.push(rem[t]!)
              const tail = [hand[tailIdx[0]!]!, hand[tailIdx[1]!]!, hand[tailIdx[2]!]!]
              if (!isLegalArrange(head, mid, tail)) continue
              const key = [evalDun(tail), evalDun(mid), evalDun(head)]
              let better = !best
              if (best && bestKey) {
                outer: for (let p = 0; p < 3; p++) {
                  for (let q = 0; q < 7; q++) {
                    if (key[p]![q] !== bestKey[p]![q]) {
                      better = key[p]![q]! > bestKey[p]![q]!
                      break outer
                    }
                  }
                }
              }
              if (better) {
                best = { head, mid, tail }
                bestKey = key
              }
            }
      }
  return best
}

export function createBijiLabClient(slot: number, deviceId: string) {
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
  /** Cards not yet placed into any dun */
  const pool = ref<number[]>([])
  const head = ref<number[]>([])
  const mid = ref<number[]>([])
  const tail = ref<number[]>([])
  /** Selected card id waiting to place into a dun */
  const selected = ref<number | null>(null)
  const locked = ref(false)
  const remainS = ref(0)
  const settleText = ref('')
  let sock: GameSocket | null = null
  let countdownTimer: number | null = null

  const canMatch = computed(() => wsOk.value && !roomId.value && !matching.value)
  const canReady = computed(() => !!roomId.value && roomPhase.value === 'WaitReady')
  const arranging = computed(
    () => !!roundId.value && !settleText.value && hand.value.length === 9 && !locked.value,
  )
  const poolFaces = computed(() => pool.value.map(bijiCardFace))
  const headFaces = computed(() => head.value.map(bijiCardFace))
  const midFaces = computed(() => mid.value.map(bijiCardFace))
  const tailFaces = computed(() => tail.value.map(bijiCardFace))
  const headType = computed(() => dunTypeLabel(head.value))
  const midType = computed(() => dunTypeLabel(mid.value))
  const tailType = computed(() => dunTypeLabel(tail.value))
  const complete = computed(
    () => head.value.length === 3 && mid.value.length === 3 && tail.value.length === 3,
  )
  const legal = computed(() =>
    complete.value ? isLegalArrange(head.value, mid.value, tail.value) : false,
  )
  const canConfirm = computed(() => arranging.value && complete.value && legal.value)
  const arrangeHint = computed(() => {
    if (!arranging.value) return ''
    if (!complete.value) return `请把 9 张牌摆满三墩（剩余 ${pool.value.length} 张）`
    if (!legal.value) return '倒水：须满足 头 ≤ 中 ≤ 尾，请调整'
    return '摆牌合法，可以确认'
  })

  function resetBoardFromHand(cards: number[]) {
    hand.value = cards.slice()
    pool.value = cards.slice().sort((a, b) => {
      if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
      return suitOf(b) - suitOf(a)
    })
    head.value = []
    mid.value = []
    tail.value = []
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
      onBijiGameStart: (s) => {
        roundId.value = s.round_id
        mySeat.value = s.self_seat
        locked.value = false
        settleText.value = ''
        roomPhase.value = 'Arrange'
        resetBoardFromHand(s.hand)
        startCountdown(s.arrange_timeout_s > 0 ? s.arrange_timeout_s : 20)
        pushLog(`GameStart timeout=${s.arrange_timeout_s}s cards=${s.hand.length}`)
      },
      onBijiArrangeState: (s) => {
        if (s.remain_s > 0) startCountdown(s.remain_s)
        else remainS.value = 0
        if (mySeat.value >= 0 && s.locked[mySeat.value]) {
          locked.value = true
          stopCountdown()
        }
      },
      onBijiArrangeAck: (s) => {
        if (s.locked) {
          locked.value = true
          stopCountdown()
        }
        pushLog(`ArrangeAck code=${s.code} ${s.message}`)
      },
      onBijiSettle: (s) => {
        stopCountdown()
        remainS.value = 0
        const me = s.seats.find((x) => x.uid === uid.value)
        settleText.value = me ? `net=${me.net} gold=${me.gold}` : 'settled'
        gold.value = me?.gold ?? gold.value
        roomPhase.value = 'WaitReady'
        pushLog(`Settle ${settleText.value}`)
      },
      onBijiSnapshot: (s) => {
        locked.value = s.locked
        if (s.has_draft && s.draft_head.length === 3) {
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
  }

  function toggleSelect(cardId: number) {
    if (locked.value || !arranging.value) return
    if (!pool.value.includes(cardId)) return
    selected.value = selected.value === cardId ? null : cardId
  }

  function placeTo(zone: 'head' | 'mid' | 'tail') {
    if (locked.value || selected.value == null) return
    const target = zone === 'head' ? head : zone === 'mid' ? mid : tail
    if (target.value.length >= 3) return
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
    pool.value = [...pool.value, cardId].sort((a, b) => {
      if (rankOf(a) !== rankOf(b)) return rankOf(b) - rankOf(a)
      return suitOf(b) - suitOf(a)
    })
    selected.value = null
  }

  function clearBoard() {
    if (locked.value || !hand.value.length) return
    resetBoardFromHand(hand.value)
  }

  /** Fill duns with heuristic; does not confirm. */
  function smartFill() {
    if (locked.value || !hand.value.length) return
    const arr = autoArrangeHand(hand.value)
    if (!arr) return
    head.value = arr.head
    mid.value = arr.mid
    tail.value = arr.tail
    pool.value = []
    selected.value = null
  }

  function confirmArrange() {
    if (!canConfirm.value) return
    sock?.bijiArrange(head.value, mid.value, tail.value, true)
  }

  function autoConfirm() {
    smartFill()
    if (canConfirm.value) confirmArrange()
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
    poolFaces,
    headFaces,
    midFaces,
    tailFaces,
    headType,
    midType,
    tailType,
    complete,
    legal,
    canConfirm,
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
    autoConfirm,
    topupGold,
  }
}
