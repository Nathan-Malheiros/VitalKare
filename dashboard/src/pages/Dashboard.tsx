import { useEffect, useCallback } from 'react'
import { useQuery } from '@tanstack/react-query'
import { BedGrid } from '@/components/beds/BedGrid'
import { EmergencyBanner } from '@/components/alerts/EmergencyBanner'
import { useWebSocket } from '@/hooks/useWebSocket'
import { useBedsStore } from '@/store/bedsStore'
import { useAlertsStore } from '@/store/alertsStore'
import type { Bed } from '@/store/bedsStore'
import api from '@/services/api'

export default function Dashboard() {
  const setBeds = useBedsStore((s) => s.setBeds)
  const updateBedStatus = useBedsStore((s) => s.updateBedStatus)
  const updateBedEnvironment = useBedsStore((s) => s.updateBedEnvironment)
  const addAlert = useAlertsStore((s) => s.addAlert)
  const acknowledgeAlert = useAlertsStore((s) => s.acknowledgeAlert)

  // =========================================================
  // CARREGA LEITOS VIA REST (inicial)
  // =========================================================
  const { data: beds } = useQuery<Bed[]>({
    queryKey: ['beds'],
    queryFn: () => api.get('/beds/').then((r) => r.data),
    refetchInterval: 60_000,
  })

  useEffect(() => {
    if (beds) setBeds(beds)
  }, [beds, setBeds])

  // =========================================================
  // ATUALIZA EM TEMPO REAL VIA WEBSOCKET
  // =========================================================
  const handleMessage = useCallback(
    (msg: Record<string, unknown>) => {
      const type = msg.type as string

      if (type === 'bed_status') {
        updateBedStatus(
          msg.bed_id as string,
          msg.status as 'ONLINE' | 'OFFLINE' | 'EMERGENCY' | 'CRITICAL_OFFLINE',
        )
      }

      if (type === 'emergency') {
        addAlert({
          id: msg.event_id as string,
          type: 'emergency',
          bed_id: msg.bed_id as string,
          ward: msg.ward as string | undefined,
          device_id: msg.device_id as string | undefined,
          timestamp: (msg.timestamp as string) ?? new Date().toISOString(),
          acknowledged: false,
        })
        // Atualiza status do leito imediatamente
        updateBedStatus(msg.bed_id as string, 'EMERGENCY')
      }

      if (type === 'environment') {
        updateBedEnvironment(
          msg.bed_id as string,
          msg.temperature_celsius as number,
          msg.humidity_percent as number,
        )
      }
    },
    [updateBedStatus, updateBedEnvironment, addAlert]
  )

  useWebSocket(handleMessage)

  const handleAcknowledge = async (bedId: string) => {
    // Localiza alerta pendente para este leito e marca como atendido
    const alert = useAlertsStore
      .getState()
      .alerts.find((a) => a.bed_id === bedId && !a.acknowledged)
    if (alert) {
      await api.post(`/events/${alert.id}/acknowledge`, { notes: '' }).catch(() => null)
      acknowledgeAlert(alert.id)
    }
    updateBedStatus(bedId, 'OCCUPIED')
  }

  return (
    <div className="min-h-screen bg-gray-100">
      <EmergencyBanner />

      {/* Header */}
      <header className="bg-white shadow-sm px-6 py-4 flex items-center justify-between">
        <h1 className="text-2xl font-bold text-brand-700">🏥 VitalKare</h1>
        <span className="text-sm text-gray-500">Monitor de Leitos em Tempo Real</span>
      </header>

      {/* Conteúdo */}
      <main className="p-6">
        <BedGrid onAcknowledge={handleAcknowledge} />
      </main>
    </div>
  )
}
