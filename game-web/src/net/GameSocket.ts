import {
  encodeFrame,
  tryDecodeFrames,
  MsgId,
  encodeC2S_Auth,
  encodeC2S_Heartbeat,
  encodeC2S_GetLobby,
  encodeC2S_QuickMatch,
  encodeC2S_CancelMatch,
  encodeC2S_Ready,
  encodeC2S_LeaveRoom,
  encodeC2S_DdzBid,
  encodeC2S_DdzPlay,
  encodeC2S_HzmjDiscard,
  encodeC2S_HzmjAction,
  encodeC2S_HzmjGang,
  encodeC2S_PhzDiscard,
  encodeC2S_PhzAction,
  encodeC2S_FishFire,
  encodeC2S_FishSetMult,
  encodeC2S_FishLeave,
  encodeC2S_BijiArrange,
  encodeC2S_ClientTrace,
  decodeS2C_AuthResult,
  decodeS2C_LobbyInfo,
  decodeS2C_MatchStatus,
  decodeS2C_RoomState,
  decodeS2C_DdzGameStart,
  decodeS2C_DdzTurn,
  decodeS2C_DdzBidBroadcast,
  decodeS2C_DdzPlayBroadcast,
  decodeS2C_DdzSettle,
  decodeS2C_DdzReconnect,
  decodeS2C_HzmjGameStart,
  decodeS2C_HzmjTurn,
  decodeS2C_HzmjDraw,
  decodeS2C_HzmjDiscardBroadcast,
  decodeS2C_HzmjActionBroadcast,
  decodeS2C_HzmjSettle,
  decodeS2C_HzmjLiuJu,
  decodeS2C_PhzGameStart,
  decodeS2C_PhzTurn,
  decodeS2C_PhzDraw,
  decodeS2C_PhzReveal,
  decodeS2C_PhzDiscardBroadcast,
  decodeS2C_PhzActionBroadcast,
  decodeS2C_PhzSettle,
  decodeS2C_PhzLiuJu,
  decodeS2C_FishGameStart,
  decodeS2C_FishSeatUpdate,
  decodeS2C_FishSpawn,
  decodeS2C_FishDespawn,
  decodeS2C_FishFireBroadcast,
  decodeS2C_FishHit,
  decodeS2C_FishCatch,
  decodeS2C_BijiGameStart,
  decodeS2C_BijiArrangeState,
  decodeS2C_BijiArrangeAck,
  decodeS2C_BijiCompare,
  decodeS2C_BijiSettle,
  decodeS2C_BijiSnapshot,
  decodeS2C_ActivityUpdate,
  decodeS2C_Error,
  decodeS2C_Kick,
  type LobbyTemplate,
  type RoomSeat,
  type HzmjGameStart,
  type HzmjSettle,
  type PhzGameStart,
  type PhzSettle,
  type FishGameStart,
  type FishSeatInfo,
  type FishSnap,
  type BijiGameStart,
  type BijiArrangeState,
  type BijiSeatSettle,
} from './frame'

export type GameHandlers = {
  onAuth?: (ok: boolean, uid: number, message: string) => void
  onHeartbeatAck?: () => void
  onKick?: (reason: number, message: string) => void
  onClose?: () => void
  onLog?: (line: string) => void
  onLobby?: (info: { templates: LobbyTemplate[]; gold: number; diamond: number }) => void
  onMatchStatus?: (s: { status: number; room_id: number; message: string }) => void
  onRoomState?: (s: { room_id: number; template_id: number; seats: RoomSeat[]; phase: string }) => void
  onGameStart?: (s: {
    seat_id: number
    hand_cards: number[]
    landlord_seat: number
    bottom_cards: number[]
    round_id: number
  }) => void
  onTurn?: (s: { seat_id: number; phase: string; timeout_s: number }) => void
  onBidBroadcast?: (s: { seat_id: number; score: number }) => void
  onPlayBroadcast?: (s: { seat_id: number; pass: boolean; cards: number[]; cards_left: number }) => void
  onSettle?: (s: {
    round_id: number
    base_score: number
    multiplier: number
    entries: { uid: number; seat_id: number; delta_gold: number }[]
  }) => void
  onReconnect?: (s: {
    seat_id: number
    phase: string
    hand: number[]
    landlord_seat: number
    current_seat: number
    timeout_s: number
  }) => void
  onError?: (s: { code: number; message: string; ref_msg_id: number }) => void
  onActivityUpdate?: (s: {
    activity_id: number
    type: string
    progress_json: string
    claimable: boolean
  }) => void
  onHzmjGameStart?: (s: HzmjGameStart) => void
  onHzmjTurn?: (s: {
    seat_id: number
    sub: string
    timeout_s: number
    wall_remain: number
    piao_seat: number
    self_hand: number[]
    can_zimo: boolean
  }) => void
  onHzmjDraw?: (s: { seat_id: number; tile: number }) => void
  onHzmjDiscard?: (s: { seat_id: number; tile: number }) => void
  onHzmjAction?: (s: {
    seat_id: number
    action: number
    tile: number
    tiles: number[]
    from_seat: number
    meld_kind: number
  }) => void
  onHzmjSettle?: (s: HzmjSettle) => void
  onHzmjLiuJu?: (s: { lian_zhuang: number }) => void
  onPhzGameStart?: (s: PhzGameStart) => void
  onPhzTurn?: (s: {
    seat_id: number
    sub: string
    timeout_s: number
    wall_remain: number
    self_hand: number[]
    can_hu: boolean
  }) => void
  onPhzDraw?: (s: { seat_id: number; tile: number }) => void
  onPhzReveal?: (s: { seat_id: number; tile: number }) => void
  onPhzDiscard?: (s: { seat_id: number; tile: number }) => void
  onPhzAction?: (s: {
    seat_id: number
    action: number
    tile: number
    tiles: number[]
    from_seat: number
    meld_kind: number
  }) => void
  onPhzSettle?: (s: PhzSettle) => void
  onPhzLiuJu?: (s: { banker_seat: number }) => void
  onFishGameStart?: (s: FishGameStart) => void
  onFishSeatUpdate?: (s: FishSeatInfo) => void
  onFishSpawn?: (fish: FishSnap[]) => void
  onFishDespawn?: (s: { fish_ids: number[]; reason: string }) => void
  onFishFire?: (s: {
    seat_id: number
    uid: number
    bullet_id: number
    mult: number
    x: number
    y: number
    vx: number
    vy: number
    client_seq: number
    gold: number
    cost: number
  }) => void
  onFishHit?: (s: { bullet_id: number; fish_id: number; seat_id: number; hp: number; hp_max: number }) => void
  onFishCatch?: (s: {
    fish_id: number
    type_id: number
    seat_id: number
    uid: number
    reward: number
    gold: number
  }) => void
  onBijiGameStart?: (s: BijiGameStart) => void
  onBijiArrangeState?: (s: BijiArrangeState) => void
  onBijiArrangeAck?: (s: { code: number; message: string; locked: boolean }) => void
  onBijiCompare?: (s: ReturnType<typeof decodeS2C_BijiCompare>) => void
  onBijiSettle?: (s: { round_id: number; seats: BijiSeatSettle[] }) => void
  onBijiSnapshot?: (s: ReturnType<typeof decodeS2C_BijiSnapshot>) => void
}

export class GameSocket {
  /** Align with server net.heartbeat_interval_s (default 15). */
  private static readonly HB_INTERVAL_MS = 15_000

  private ws: WebSocket | null = null
  private buf = new Uint8Array(0)
  private hbTimer: number | null = null
  private handlers: GameHandlers
  private intentionalClose = false

  constructor(handlers: GameHandlers = {}) {
    this.handlers = handlers
  }

  connect(url: string, token: string) {
    this.close(false)
    this.intentionalClose = false
    this.ws = new WebSocket(url)
    this.ws.binaryType = 'arraybuffer'
    this.ws.onopen = () => {
      this.log('WSS open, sending Auth')
      this.send(MsgId.C2S_Auth, encodeC2S_Auth(token))
    }
    this.ws.onmessage = (ev) => {
      const chunk = new Uint8Array(ev.data as ArrayBuffer)
      const merged = new Uint8Array(this.buf.length + chunk.length)
      merged.set(this.buf)
      merged.set(chunk, this.buf.length)
      const { frames, rest } = tryDecodeFrames(merged)
      this.buf = rest
      for (const f of frames) this.onFrame(f.msgId, f.body)
    }
    this.ws.onclose = () => {
      this.log('WSS closed')
      this.stopHb()
      if (!this.intentionalClose) this.handlers.onClose?.()
    }
    this.ws.onerror = () => this.log('WSS error')
  }

  getLobby() {
    this.send(MsgId.C2S_GetLobby, encodeC2S_GetLobby())
  }
  quickMatch(templateId: number) {
    this.send(MsgId.C2S_QuickMatch, encodeC2S_QuickMatch(templateId))
  }
  cancelMatch() {
    this.send(MsgId.C2S_CancelMatch, encodeC2S_CancelMatch())
  }
  ready(ready: boolean) {
    this.send(MsgId.C2S_Ready, encodeC2S_Ready(ready))
  }
  leaveRoom() {
    this.send(MsgId.C2S_LeaveRoom, encodeC2S_LeaveRoom())
  }
  bid(score: number) {
    this.send(MsgId.C2S_DdzBid, encodeC2S_DdzBid(score))
  }
  play(pass: boolean, cards: number[]) {
    this.send(MsgId.C2S_DdzPlay, encodeC2S_DdzPlay(pass, cards))
  }
  hzmjDiscard(tile: number) {
    this.send(MsgId.C2S_HzmjDiscard, encodeC2S_HzmjDiscard(tile))
  }
  hzmjAction(action: number, chiHand: number[] = []) {
    this.send(MsgId.C2S_HzmjAction, encodeC2S_HzmjAction(action, chiHand))
  }
  hzmjGang(kind: number, tile: number) {
    this.send(MsgId.C2S_HzmjGang, encodeC2S_HzmjGang(kind, tile))
  }
  phzDiscard(tile: number) {
    this.send(MsgId.C2S_PhzDiscard, encodeC2S_PhzDiscard(tile))
  }
  phzAction(action: number, chiHand: number[] = []) {
    this.send(MsgId.C2S_PhzAction, encodeC2S_PhzAction(action, chiHand))
  }
  fishFire(mult: number, aim_x: number, aim_y: number, client_seq: number, lock_fish_id = 0) {
    this.send(MsgId.C2S_FishFire, encodeC2S_FishFire(mult, aim_x, aim_y, client_seq, lock_fish_id))
  }
  fishSetMult(mult: number) {
    this.send(MsgId.C2S_FishSetMult, encodeC2S_FishSetMult(mult))
  }
  fishLeave() {
    this.send(MsgId.C2S_FishLeave, encodeC2S_FishLeave())
  }

  bijiArrange(head: number[], mid: number[], tail: number[], confirm: boolean) {
    this.send(MsgId.C2S_BijiArrange, encodeC2S_BijiArrange(head, mid, tail, confirm))
  }
  trace(roundId: number, seatId: number, game: string, event: string, detail: string) {
    if (roundId <= 0) return
    this.send(MsgId.C2S_ClientTrace, encodeC2S_ClientTrace(roundId, seatId, event, detail, game))
  }

  private onFrame(msgId: number, body: Uint8Array) {
    if (msgId === MsgId.S2C_AuthResult) {
      const r = decodeS2C_AuthResult(body)
      this.log(`AuthResult code=${r.code} uid=${r.uid}`)
      if (r.code === 0) this.startHb()
      else this.stopHb()
      this.handlers.onAuth?.(r.code === 0, r.uid, r.message)
      return
    }
    if (msgId === MsgId.S2C_HeartbeatAck) {
      this.handlers.onHeartbeatAck?.()
      return
    }
    if (msgId === MsgId.S2C_Kick) {
      const k = decodeS2C_Kick(body)
      this.log(`Kicked reason=${k.reason} ${k.message}`)
      this.stopHb()
      this.handlers.onKick?.(k.reason, k.message)
      return
    }
    if (msgId === MsgId.S2C_LobbyInfo) {
      const r = decodeS2C_LobbyInfo(body)
      this.handlers.onLobby?.(r)
      return
    }
    if (msgId === MsgId.S2C_MatchStatus) {
      const r = decodeS2C_MatchStatus(body)
      this.log(`MatchStatus ${r.status} room=${r.room_id} ${r.message}`)
      this.handlers.onMatchStatus?.(r)
      return
    }
    if (msgId === MsgId.S2C_RoomState) {
      const r = decodeS2C_RoomState(body)
      this.handlers.onRoomState?.(r)
      return
    }
    if (msgId === MsgId.S2C_DdzGameStart) {
      const r = decodeS2C_DdzGameStart(body)
      this.handlers.onGameStart?.(r)
      return
    }
    if (msgId === MsgId.S2C_DdzTurn) {
      const r = decodeS2C_DdzTurn(body)
      this.handlers.onTurn?.(r)
      return
    }
    if (msgId === MsgId.S2C_DdzBidBroadcast) {
      this.handlers.onBidBroadcast?.(decodeS2C_DdzBidBroadcast(body))
      return
    }
    if (msgId === MsgId.S2C_DdzPlayBroadcast) {
      this.handlers.onPlayBroadcast?.(decodeS2C_DdzPlayBroadcast(body))
      return
    }
    if (msgId === MsgId.S2C_DdzSettle) {
      this.handlers.onSettle?.(decodeS2C_DdzSettle(body))
      return
    }
    if (msgId === MsgId.S2C_DdzReconnect) {
      const r = decodeS2C_DdzReconnect(body)
      this.log(`DdzReconnect seat=${r.seat_id} phase=${r.phase} hand=${r.hand.length}`)
      this.handlers.onReconnect?.(r)
      return
    }
    if (msgId === MsgId.S2C_HzmjGameStart) {
      const r = decodeS2C_HzmjGameStart(body)
      this.log(`HzmjStart seat=${r.self_seat} hand=${r.self_hand.length} wall=${r.wall_remain}`)
      this.handlers.onHzmjGameStart?.(r)
      return
    }
    if (msgId === MsgId.S2C_HzmjTurn) {
      const r = decodeS2C_HzmjTurn(body)
      this.handlers.onHzmjTurn?.(r)
      return
    }
    if (msgId === MsgId.S2C_HzmjDraw) {
      this.handlers.onHzmjDraw?.(decodeS2C_HzmjDraw(body))
      return
    }
    if (msgId === MsgId.S2C_HzmjDiscardBroadcast) {
      this.handlers.onHzmjDiscard?.(decodeS2C_HzmjDiscardBroadcast(body))
      return
    }
    if (msgId === MsgId.S2C_HzmjActionBroadcast) {
      this.handlers.onHzmjAction?.(decodeS2C_HzmjActionBroadcast(body))
      return
    }
    if (msgId === MsgId.S2C_HzmjSettle) {
      const r = decodeS2C_HzmjSettle(body)
      this.log(
        `HzmjSettle ${r.is_zimo ? '自摸' : '点炮'} M=${r.M} N=${r.N} winner=${r.winner_seat}` +
          (r.is_zimo ? '' : ` shooter=${r.shooter_seat}`) +
          (r.hu_tile >= 0 ? ` tile=${r.hu_tile}` : ''),
      )
      this.handlers.onHzmjSettle?.(r)
      return
    }
    if (msgId === MsgId.S2C_HzmjLiuJu) {
      const r = decodeS2C_HzmjLiuJu(body)
      this.log(`HzmjLiuJu lian=${r.lian_zhuang}`)
      this.handlers.onHzmjLiuJu?.(r)
      return
    }
    if (msgId === MsgId.S2C_PhzGameStart) {
      const r = decodeS2C_PhzGameStart(body)
      this.log(`PhzStart seat=${r.self_seat} hand=${r.self_hand.length} wall=${r.wall_remain}`)
      this.handlers.onPhzGameStart?.(r)
      return
    }
    if (msgId === MsgId.S2C_PhzTurn) {
      this.handlers.onPhzTurn?.(decodeS2C_PhzTurn(body))
      return
    }
    if (msgId === MsgId.S2C_PhzDraw) {
      this.handlers.onPhzDraw?.(decodeS2C_PhzDraw(body))
      return
    }
    if (msgId === MsgId.S2C_PhzReveal) {
      this.handlers.onPhzReveal?.(decodeS2C_PhzReveal(body))
      return
    }
    if (msgId === MsgId.S2C_PhzDiscardBroadcast) {
      this.handlers.onPhzDiscard?.(decodeS2C_PhzDiscardBroadcast(body))
      return
    }
    if (msgId === MsgId.S2C_PhzActionBroadcast) {
      this.handlers.onPhzAction?.(decodeS2C_PhzActionBroadcast(body))
      return
    }
    if (msgId === MsgId.S2C_PhzSettle) {
      const r = decodeS2C_PhzSettle(body)
      this.log(`PhzSettle xi=${r.hu_xi} tun=${r.tun} fan=${r.fan} winner=${r.winner_seat}`)
      this.handlers.onPhzSettle?.(r)
      return
    }
    if (msgId === MsgId.S2C_PhzLiuJu) {
      const r = decodeS2C_PhzLiuJu(body)
      this.log(`PhzLiuJu banker=${r.banker_seat}`)
      this.handlers.onPhzLiuJu?.(r)
      return
    }
    if (msgId === MsgId.S2C_FishGameStart) {
      const r = decodeS2C_FishGameStart(body)
      this.log(`FishStart seat=${r.self_seat} fish=${r.fish.length}`)
      this.handlers.onFishGameStart?.(r)
      return
    }
    if (msgId === MsgId.S2C_FishSeatUpdate) {
      const r = decodeS2C_FishSeatUpdate(body)
      if (r) this.handlers.onFishSeatUpdate?.(r)
      return
    }
    if (msgId === MsgId.S2C_FishSpawn) {
      this.handlers.onFishSpawn?.(decodeS2C_FishSpawn(body))
      return
    }
    if (msgId === MsgId.S2C_FishDespawn) {
      this.handlers.onFishDespawn?.(decodeS2C_FishDespawn(body))
      return
    }
    if (msgId === MsgId.S2C_FishFireBroadcast) {
      this.handlers.onFishFire?.(decodeS2C_FishFireBroadcast(body))
      return
    }
    if (msgId === MsgId.S2C_FishHit) {
      this.handlers.onFishHit?.(decodeS2C_FishHit(body))
      return
    }
    if (msgId === MsgId.S2C_FishCatch) {
      const r = decodeS2C_FishCatch(body)
      this.log(`FishCatch fish=${r.fish_id} reward=${r.reward}`)
      this.handlers.onFishCatch?.(r)
      return
    }
    if (msgId === MsgId.S2C_BijiGameStart) {
      const r = decodeS2C_BijiGameStart(body)
      this.log(`BijiStart seat=${r.self_seat} hand=${r.hand.length}`)
      this.handlers.onBijiGameStart?.(r)
      return
    }
    if (msgId === MsgId.S2C_BijiArrangeState) {
      this.handlers.onBijiArrangeState?.(decodeS2C_BijiArrangeState(body))
      return
    }
    if (msgId === MsgId.S2C_BijiArrangeAck) {
      this.handlers.onBijiArrangeAck?.(decodeS2C_BijiArrangeAck(body))
      return
    }
    if (msgId === MsgId.S2C_BijiCompare) {
      this.handlers.onBijiCompare?.(decodeS2C_BijiCompare(body))
      return
    }
    if (msgId === MsgId.S2C_BijiSettle) {
      const r = decodeS2C_BijiSettle(body)
      this.log(`BijiSettle seats=${r.seats.length}`)
      this.handlers.onBijiSettle?.(r)
      return
    }
    if (msgId === MsgId.S2C_BijiSnapshot) {
      this.handlers.onBijiSnapshot?.(decodeS2C_BijiSnapshot(body))
      return
    }
    if (msgId === MsgId.S2C_ActivityUpdate) {
      const r = decodeS2C_ActivityUpdate(body)
      this.log(`ActivityUpdate id=${r.activity_id} type=${r.type} claimable=${r.claimable}`)
      this.handlers.onActivityUpdate?.(r)
      return
    }
    if (msgId === MsgId.S2C_Error) {
      const r = decodeS2C_Error(body)
      this.log(`Error ${r.code}: ${r.message}`)
      this.handlers.onError?.(r)
      return
    }
    this.log(`recv msg_id=${msgId} len=${body.length}`)
  }

  private send(msgId: number, body: Uint8Array) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return
    this.ws.send(encodeFrame(msgId, body))
  }

  private startHb() {
    this.stopHb()
    const beat = () => this.send(MsgId.C2S_Heartbeat, encodeC2S_Heartbeat(Date.now()))
    beat()
    this.hbTimer = window.setInterval(beat, GameSocket.HB_INTERVAL_MS)
  }

  private stopHb() {
    if (this.hbTimer != null) {
      clearInterval(this.hbTimer)
      this.hbTimer = null
    }
  }

  close(intentional = true) {
    this.intentionalClose = intentional
    this.stopHb()
    this.ws?.close()
    this.ws = null
    this.buf = new Uint8Array(0)
  }

  private log(line: string) {
    this.handlers.onLog?.(line)
  }
}
