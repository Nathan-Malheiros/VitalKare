import { create } from 'zustand'
import { immer } from 'zustand/middleware/immer'

export type BedStatus = 'ONLINE' | 'OFFLINE' | 'EMERGENCY' | 'OCCUPIED' | 'FREE' | 'CRITICAL_OFFLINE'

export interface Bed {
  id: string
  bed_number: string
  ward_id: string
  device_id: string
  status: BedStatus
  last_heartbeat: string | null
  firmware_version: string | null
  wifi_rssi: number | null
  patient_name?: string
  temperature_celsius?: number
  humidity_percent?: number
}

interface BedsState {
  beds: Record<string, Bed>
  setBeds: (beds: Bed[]) => void
  updateBedStatus: (bedId: string, status: BedStatus, extra?: Partial<Bed>) => void
  updateBedEnvironment: (bedId: string, temp: number, hum: number) => void
}

export const useBedsStore = create<BedsState>()(
  immer((set) => ({
    beds: {},

    setBeds: (beds) => {
      set((state) => {
        state.beds = {}
        beds.forEach((b) => { state.beds[b.bed_number] = b })
      })
    },

    updateBedStatus: (bedId, status, extra) => {
      set((state) => {
        const bed = Object.values(state.beds).find((b) => b.bed_number === bedId)
        if (bed) {
          bed.status = status
          if (extra) Object.assign(bed, extra)
        }
      })
    },

    updateBedEnvironment: (bedId, temp, hum) => {
      set((state) => {
        const bed = Object.values(state.beds).find((b) => b.bed_number === bedId)
        if (bed) {
          bed.temperature_celsius = temp
          bed.humidity_percent = hum
        }
      })
    },
  }))
)
