import { computed, ref } from 'vue'
import { labTopupGold, loginGuest } from '../api'
import { GameSocket } from '../net/GameSocket'
import type { FishSnap, LobbyTemplate, RoomSeat } from '../net/frame'

const WSS_URL = import.meta.env.VITE_WSS_URL || 'wss://127.0.0.1:8444/'

export function createFishLabClient(slot: number, deviceId: string) {
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
  const matchMsg = ref('')
  const roomId = ref(0)
  const roomPhase = ref('')
  const seats = ref<RoomSeat[]>([])
  const mySeat = ref(-1)
  const roundId = ref(0)
  const cannonMults = ref<number[]>([1, 2, 5, 10])
  const mult = ref(1)
  const fishList = ref<FishSnap[]>([])
  const bullets = ref<{ id: number; x: number; y: number; vx: number; vy: number }[]>([])
  const aimX = ref(960)
  const aimY = ref(540)
  const baseScore = ref(100)
  let sock: GameSocket | null = null
  let fireSeq = 0
  let animTimer: number | null = null

  const fireCost = computed(() => mult.value * baseScore.value)

  function pushLog(line: string) {
    const t = new Date().toLocaleTimeString()
    logs.value = [`[${t}] ${line}`, ...logs.value].slice(0, 40)
  }

  function upsertFish(list: FishSnap[]) {
    const map = new Map(fishList.value.map((f) => [f.fish_id, f]))
    for (const f of list) map.set(f.fish_id, f)
    fishList.value = [...map.values()]
  }

  function startAnim() {
    if (animTimer != null) return
    animTimer = window.setInterval(() => {
      const dt = 0.05
      fishList.value = fishList.value.map((f) => ({
        ...f,
        x: f.x + f.vx * dt,
        y: f.y + f.vy * dt,
      }))
      bullets.value = bullets.value
        .map((b) => ({ ...b, x: b.x + b.vx * dt, y: b.y + b.vy * dt }))
        .filter((b) => b.x > -100 && b.x < 2100 && b.y > -100 && b.y < 1200)
    }, 50)
  }

  function stopAnim() {
    if (animTimer != null) {
      clearInterval(animTimer)
      animTimer = null
    }
  }

  async function login() {
    busy.value = true
    try {
      const r = await loginGuest(deviceId)
      if (r.code !== 0 || !r.data?.access_token) {
        throw new Error(r.message || 'login failed')
      }
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
        templates.value = info.templates.filter((t) => t.game_id === 5000 || t.id === 4)
        gold.value = info.gold
      },
      onMatchStatus: (s) => {
        matching.value = s.status === 0
        matchMsg.value = s.message
        if (s.status === 1 && s.room_id) {
          roomId.value = s.room_id
          matching.value = false
        }
      },
      onRoomState: (s) => {
        roomId.value = s.room_id
        roomPhase.value = s.phase
        seats.value = s.seats
        const me = s.seats.find((x) => x.uid === uid.value)
        if (me) mySeat.value = me.seat_id
      },
      onFishGameStart: (g) => {
        roundId.value = g.round_id
        if (g.room_id) roomId.value = g.room_id
        roomPhase.value = 'Playing'
        mySeat.value = g.self_seat
        if (g.base_score > 0) baseScore.value = g.base_score
        cannonMults.value = g.cannon_mults.length ? g.cannon_mults : cannonMults.value
        mult.value = cannonMults.value[0] || 1
        fishList.value = g.fish
        const me = g.seats.find((s) => s.seat_id === g.self_seat)
        if (me) gold.value = me.gold
        startAnim()
        pushLog(`fish start room=${g.room_id} round=${g.round_id} gold=${gold.value}`)
      },
      onFishSpawn: (list) => upsertFish(list),
      onFishDespawn: (s) => {
        const dead = new Set(s.fish_ids)
        fishList.value = fishList.value.filter((f) => !dead.has(f.fish_id))
      },
      onFishFire: (f) => {
        if (f.uid === uid.value) gold.value = f.gold
        bullets.value = [
          ...bullets.value,
          { id: f.bullet_id, x: f.x, y: f.y, vx: f.vx, vy: f.vy },
        ].slice(-40)
      },
      onFishHit: (h) => {
        fishList.value = fishList.value.map((f) =>
          f.fish_id === h.fish_id ? { ...f, hp: h.hp, hp_max: h.hp_max } : f,
        )
      },
      onFishCatch: (c) => {
        fishList.value = fishList.value.filter((f) => f.fish_id !== c.fish_id)
        if (c.uid === uid.value) gold.value = c.gold
        pushLog(`catch +${c.reward}`)
      },
      onError: (e) => {
        if (e.code === 5000003) {
          errorBanner.value = `余额不足（本发 ${fireCost.value}，当前 ${gold.value}），请点「补给金币」`
        } else {
          errorBanner.value = `${e.code} ${e.message}`
        }
      },
    })
    sock.connect(WSS_URL, token.value)
  }

  async function topupGold(amount = 100000) {
    if (!token.value) return
    try {
      const r = await labTopupGold(token.value, amount)
      if (r.code !== 0) throw new Error(r.message || 'topup failed')
      gold.value = r.data.gold
      errorBanner.value = ''
      pushLog(`补给金币 +${r.data.added} → ${r.data.gold}`)
    } catch (e: unknown) {
      errorBanner.value = String(e)
    }
  }

  function match(templateId = 4) {
    if (!wsOk.value || roomId.value || matching.value) return
    const tid = templates.value.find((t) => t.game_id === 5000 || t.id === 4)?.id ?? templateId
    matching.value = true
    matchMsg.value = 'matching'
    errorBanner.value = ''
    sock?.quickMatch(tid)
  }

  function ready() {
    if (!roomId.value || roundId.value) return
    if (seatSelf.value?.ready) return
    sock?.ready(true)
  }

  /** One-shot: match fish template. */
  function oneClickMatch() {
    match(4)
  }

  /** One-shot: ready when seated and not started. */
  function oneClickReady() {
    ready()
  }

  function fire() {
    if (!roundId.value) return
    if (gold.value < fireCost.value) {
      errorBanner.value = `余额不足（本发 ${fireCost.value}，当前 ${gold.value}），请点「补给金币」`
      return
    }
    fireSeq += 1
    sock?.fishFire(mult.value, aimX.value, aimY.value, fireSeq)
  }

  function setMult(m: number) {
    mult.value = m
    sock?.fishSetMult(m)
  }

  function leave() {
    if (matching.value) {
      sock?.cancelMatch()
      matching.value = false
      matchMsg.value = ''
    }
    if (roomId.value || roundId.value) {
      sock?.fishLeave()
      sock?.leaveRoom()
    }
    stopAnim()
    fishList.value = []
    bullets.value = []
    roomId.value = 0
    roundId.value = 0
    roomPhase.value = ''
    seats.value = []
    mySeat.value = -1
    pushLog('left room')
  }

  /** One-shot leave (cancel match / leave seat / clear table). */
  function oneClickLeave() {
    leave()
  }

  const seatSelf = computed(() => seats.value.find((s) => s.seat_id === mySeat.value) || null)
  const iAmReady = computed(() => !!seatSelf.value?.ready)
  const canMatch = computed(() => wsOk.value && !roomId.value && !matching.value && !busy.value)
  const canReady = computed(() => !!roomId.value && !roundId.value && !iAmReady.value)

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
    matchMsg,
    roomId,
    roomPhase,
    seats,
    mySeat,
    roundId,
    cannonMults,
    mult,
    fishList,
    bullets,
    aimX,
    aimY,
    baseScore,
    fireCost,
    seatSelf,
    iAmReady,
    canMatch,
    canReady,
    login,
    match,
    ready,
    oneClickMatch,
    oneClickReady,
    oneClickLeave,
    topupGold,
    fire,
    setMult,
    leave,
  }
}
