import { useAlertsStore } from '@/store/alertsStore'
import api from '@/services/api'

export function EmergencyBanner() {
  const alerts = useAlertsStore((s) =>
    s.alerts.filter((a) => a.type === 'emergency' && !a.acknowledged)
  )
  const acknowledgeAlert = useAlertsStore((s) => s.acknowledgeAlert)

  if (alerts.length === 0) return null

  const handleAck = async (alertId: string, eventId: string) => {
    await api.post(`/events/${eventId}/acknowledge`, { notes: '' }).catch(() => null)
    acknowledgeAlert(alertId)
  }

  return (
    <div className="fixed top-0 left-0 right-0 z-50 space-y-1">
      {alerts.map((alert) => (
        <div
          key={alert.id}
          className="flex items-center justify-between bg-red-600 px-6 py-3 text-white shadow-lg"
        >
          <div className="flex items-center gap-3">
            <span className="text-2xl">🚨</span>
            <div>
              <p className="font-bold text-lg">EMERGÊNCIA — Leito {alert.bed_id}</p>
              <p className="text-sm opacity-90">
                {alert.ward && `Ala: ${alert.ward} · `}
                {new Date(alert.timestamp).toLocaleTimeString('pt-BR')}
              </p>
            </div>
          </div>
          <button
            onClick={() => handleAck(alert.id, alert.id)}
            className="rounded-lg border-2 border-white px-4 py-1.5 font-semibold hover:bg-red-700 transition text-sm"
          >
            Confirmar Atendimento
          </button>
        </div>
      ))}
    </div>
  )
}
