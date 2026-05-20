/**
 * devicesStore — mirrors the MVP server's device model.
 * Updated from WebSocket snapshot + telemetry + event messages.
 */
import { create } from 'zustand'
import { immer } from 'zustand/middleware/immer'

export type DeviceStatus = 'ONLINE' | 'OFFLINE' | 'EMERGENCY' | 'PENDING'

export interface DeviceEvent {
  id: number
  device_id: string
  event_type: 'REQUEST' | 'EMERGENCY'
  timestamp: string
  acknowledged: number  // 0 or 1
}

export interface AudioSession {
  session_id: number
  device_id: string
  trigger: number
  start_time: number | null
  end_time: number | null
  total_chunks: number | null
  file_path: string | null
  status: 'RECORDING' | 'COMPLETE' | 'ERROR'
}

export interface Device {
  device_id: string
  ward_id: string
  fw_version: string
  capabilities: number
  approved: number
  patient_name: string | null
  room_label: string | null
  status: DeviceStatus
  last_seen: number | null
  rssi: number | null
  free_heap_bytes: number | null
  uptime_ms: number | null
  bpm: number | null
  spo2_pct: number | null
  finger_detected: number | null
  temperature_c: number | null
  humidity_pct: number | null
  light_lux: number | null
  noise_db: number | null
  created_at: number
}

interface DevicesState {
  devices: Record<string, Device>
  events: DeviceEvent[]
  setSnapshot: (devices: Device[], events: DeviceEvent[]) => void
  upsertDevice: (device: Partial<Device> & { device_id: string }) => void
  updateTelemetry: (device_id: string, data: Partial<Device>) => void
  setDeviceStatus: (device_id: string, status: DeviceStatus) => void
  addEvent: (event: DeviceEvent) => void
  acknowledgeEvent: (event_id: number) => void
  removeDevice: (device_id: string) => void
}

export const useDevicesStore = create<DevicesState>()(
  immer((set) => ({
    devices: {},
    events: [],

    setSnapshot: (devices, events) => {
      set((state) => {
        state.devices = {}
        devices.forEach((d) => { state.devices[d.device_id] = d })
        state.events = events.slice(0, 100)
      })
    },

    upsertDevice: (device) => {
      set((state) => {
        const existing = state.devices[device.device_id]
        if (existing) {
          Object.assign(existing, device)
        } else {
          state.devices[device.device_id] = {
            ward_id: '',
            fw_version: '',
            capabilities: 0,
            approved: 0,
            patient_name: null,
            room_label: null,
            status: 'OFFLINE',
            last_seen: null,
            rssi: null,
            free_heap_bytes: null,
            uptime_ms: null,
            bpm: null,
            spo2_pct: null,
            finger_detected: null,
            temperature_c: null,
            humidity_pct: null,
            light_lux: null,
            noise_db: null,
            created_at: Date.now() / 1000,
            ...device,
          } as Device
        }
      })
    },

    updateTelemetry: (device_id, data) => {
      set((state) => {
        if (state.devices[device_id]) {
          Object.assign(state.devices[device_id], data)
        }
      })
    },

    setDeviceStatus: (device_id, status) => {
      set((state) => {
        if (state.devices[device_id]) {
          state.devices[device_id].status = status
          state.devices[device_id].last_seen = Date.now() / 1000
        }
      })
    },

    addEvent: (event) => {
      set((state) => {
        const exists = state.events.some((e) => e.id === event.id)
        if (!exists) {
          state.events.unshift(event)
          if (state.events.length > 100) state.events.splice(100)
        }
      })
    },

    acknowledgeEvent: (event_id) => {
      set((state) => {
        const ev = state.events.find((e) => e.id === event_id)
        if (ev) ev.acknowledged = 1
      })
    },

    removeDevice: (device_id) => {
      set((state) => {
        delete state.devices[device_id]
      })
    },
  }))
)
