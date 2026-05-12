import { useEffect, useRef, useCallback } from 'react'
import { useAuthStore } from '@/store/authStore'

const WS_URL = `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}/ws/dashboard`

type MessageHandler = (data: Record<string, unknown>) => void

/**
 * Hook de WebSocket autenticado com reconexão automática.
 *
 * @param onMessage - Callback chamado para cada mensagem recebida.
 * @param reconnectDelayMs - Atraso inicial de reconexão (dobra a cada tentativa, máx 30s).
 */
export function useWebSocket(
  onMessage: MessageHandler,
  reconnectDelayMs = 2000
): void {
  const wsRef = useRef<WebSocket | null>(null)
  const delayRef = useRef(reconnectDelayMs)
  const unmountedRef = useRef(false)
  const token = useAuthStore((s) => s.accessToken)

  const connect = useCallback(() => {
    if (unmountedRef.current || !token) return

    const ws = new WebSocket(WS_URL)
    wsRef.current = ws

    ws.onopen = () => {
      delayRef.current = reconnectDelayMs  // reset ao conectar
      ws.send(JSON.stringify({ type: 'auth', token }))
    }

    ws.onmessage = (evt) => {
      try {
        const data = JSON.parse(evt.data) as Record<string, unknown>
        onMessage(data)
      } catch {
        // mensagem inválida — ignorar
      }
    }

    ws.onclose = () => {
      if (unmountedRef.current) return
      const delay = Math.min(delayRef.current, 30_000)
      delayRef.current = delay * 2
      setTimeout(connect, delay)
    }

    ws.onerror = () => {
      ws.close()
    }
  }, [token, onMessage, reconnectDelayMs])

  useEffect(() => {
    unmountedRef.current = false
    connect()
    return () => {
      unmountedRef.current = true
      wsRef.current?.close()
    }
  }, [connect])
}
