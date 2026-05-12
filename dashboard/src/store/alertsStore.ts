import { create } from 'zustand'
import { immer } from 'zustand/middleware/immer'

export interface Alert {
  id: string
  type: 'emergency' | 'request' | 'offline'
  bed_id: string
  ward?: string
  device_id?: string
  timestamp: string
  acknowledged: boolean
}

interface AlertsState {
  alerts: Alert[]
  addAlert: (alert: Alert) => void
  acknowledgeAlert: (id: string) => void
  clearResolved: () => void
}

export const useAlertsStore = create<AlertsState>()(
  immer((set) => ({
    alerts: [],

    addAlert: (alert) => {
      set((state) => {
        // Evita duplicatas pelo id
        const exists = state.alerts.some((a) => a.id === alert.id)
        if (!exists) {
          state.alerts.unshift(alert)
          // Mantém no máximo 100 alertas em memória
          if (state.alerts.length > 100) state.alerts.splice(100)
        }
      })
    },

    acknowledgeAlert: (id) => {
      set((state) => {
        const alert = state.alerts.find((a) => a.id === id)
        if (alert) alert.acknowledged = true
      })
    },

    clearResolved: () => {
      set((state) => {
        state.alerts = state.alerts.filter((a) => !a.acknowledged)
      })
    },
  }))
)
