/**
 * VitalKare — WebSocket Service Singleton
 *
 * Gerencia a conexão WebSocket com reconexão automática e backoff exponencial.
 * Pode ser usado fora de componentes React (ex.: stores Zustand).
 *
 * Uso:
 *   import { websocketService } from '@/services/websocketService'
 *   websocketService.connect(token)
 *   websocketService.on('emergency', handler)
 *   websocketService.disconnect()
 */

type MessageType = string
type MessageHandler = (payload: unknown) => void

const WS_BASE = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws/dashboard`

const RECONNECT_INITIAL_MS = 2_000
const RECONNECT_MAX_MS = 30_000

class WebSocketService {
  private ws: WebSocket | null = null
  private token: string | null = null
  private reconnectDelay = RECONNECT_INITIAL_MS
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null
  private destroyed = false

  /** Map de tipo → set de handlers */
  private handlers: Map<MessageType, Set<MessageHandler>> = new Map()

  // =========================================================
  // API pública
  // =========================================================

  /** Abre a conexão e autentica com o token fornecido. */
  connect(token: string): void {
    this.token = token
    this.destroyed = false
    this._clearReconnectTimer()
    this._open()
  }

  /** Fecha a conexão e desativa reconexão automática. */
  disconnect(): void {
    this.destroyed = true
    this._clearReconnectTimer()
    if (this.ws) {
      this.ws.onclose = null
      this.ws.close()
      this.ws = null
    }
  }

  /**
   * Registra um handler para um tipo de mensagem específico.
   * Use `'*'` para receber todas as mensagens.
   */
  on(type: MessageType, handler: MessageHandler): () => void {
    if (!this.handlers.has(type)) {
      this.handlers.set(type, new Set())
    }
    this.handlers.get(type)!.add(handler)
    // Retorna função de cleanup
    return () => this.off(type, handler)
  }

  /** Remove um handler registrado. */
  off(type: MessageType, handler: MessageHandler): void {
    this.handlers.get(type)?.delete(handler)
  }

  /** Envia uma mensagem serializada para o servidor (apenas se conectado). */
  send(payload: unknown): void {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(payload))
    }
  }

  get isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN
  }

  // =========================================================
  // Internos
  // =========================================================

  private _open(): void {
    if (!this.token) return

    const ws = new WebSocket(WS_BASE)
    this.ws = ws

    ws.onopen = () => {
      this.reconnectDelay = RECONNECT_INITIAL_MS
      // Autentica enviando o token logo após conectar
      ws.send(JSON.stringify({ type: 'auth', token: this.token }))
    }

    ws.onmessage = (evt: MessageEvent) => {
      let data: Record<string, unknown>
      try {
        data = JSON.parse(evt.data as string) as Record<string, unknown>
      } catch {
        return // payload inválido — ignorar
      }

      const type = (data.type as string) ?? 'unknown'

      // Notifica handlers específicos do tipo
      this.handlers.get(type)?.forEach((h) => h(data))
      // Notifica handlers curinga
      this.handlers.get('*')?.forEach((h) => h(data))
    }

    ws.onclose = () => {
      this.ws = null
      if (!this.destroyed) {
        this._scheduleReconnect()
      }
    }

    ws.onerror = () => {
      // onclose será chamado logo em seguida
      ws.close()
    }
  }

  private _scheduleReconnect(): void {
    const delay = Math.min(this.reconnectDelay, RECONNECT_MAX_MS)
    this.reconnectDelay = delay * 2
    this.reconnectTimer = setTimeout(() => {
      if (!this.destroyed) this._open()
    }, delay)
  }

  private _clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
  }
}

/** Instância singleton — use diretamente nos stores e hooks. */
export const websocketService = new WebSocketService()
