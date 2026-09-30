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
  decodeS2C_ActivityUpdate,
  decodeS2C_Error,
  type LobbyTemplate,
  type RoomSeat,
  type HzmjGameStart,
  type HzmjSettle,
} from './frame'

export type GameHandlers = {
  onAuth?: (ok: boolean, uid: number, message: string) => void
  onHeartbeatAck?: () => void
  onKick?: () => void
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
      this.log('Kicked')
      this.handlers.onKick?.()
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
