import { useCallback, useState, useEffect, useRef } from 'react'
import { useNavigate } from 'react-router-dom'
import { useWebSocket } from '@/hooks/useWebSocket'
import { useAuthStore } from '@/store/authStore'
import { useDevicesStore, type Device, type DeviceEvent } from '@/store/devicesStore'
import { DeviceCard } from '@/components/devices/DeviceCard'
import api from '@/services/api'

type FilterKey = 'ALL' | 'ONLINE' | 'EMERGENCY' | 'OFFLINE' | 'PENDING'

const FILTERS: { key: FilterKey; label: string }[] = [
  { key: 'ALL',       label: 'Todos'      },
  { key: 'ONLINE',    label: 'Online'     },
  { key: 'EMERGENCY', label: 'Emergência' },
  { key: 'OFFLINE',   label: 'Offline'    },
  { key: 'PENDING',   label: 'Pendentes'  },
]

const LOG_TYPE_STYLE: Record<string, string> = {
  EMERGENCY: 'bg-red-900/60 text-red-300',
  REQUEST:   'bg-blue-900/60 text-blue-300',
  ack:       'bg-green-900/60 text-green-300',
}

function playAlertBeep() {
  try {
    const ctx  = new AudioContext()
    const osc  = ctx.createOscillator()
    const gain = ctx.createGain()
    osc.connect(gain); gain.connect(ctx.destination)
    osc.frequency.value = 880
    gain.gain.setValueAtTime(0.3, ctx.currentTime)
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.8)
    osc.start(); osc.stop(ctx.currentTime + 0.8)
  } catch { /* ignore */ }
}

function relTime(iso: string): string {
  const s = Math.round((Date.now() - new Date(iso).getTime()) / 1000)
  if (s <  5)   return 'agora'
  if (s < 60)   return `há ${s}s`
  if (s < 3600) return `há ${Math.floor(s / 60)}m`
  return `há ${Math.floor(s / 3600)}h`
}

interface LogEntry {
  id: number
  type: string
  src: string
  msg: string
  ts: string
}

export default function Dashboard() {
  const devices          = useDevicesStore((s) => s.devices)
  const events           = useDevicesStore((s) => s.events)
  const setSnapshot      = useDevicesStore((s) => s.setSnapshot)
  const upsertDevice     = useDevicesStore((s) => s.upsertDevice)
  const updateTelemetry  = useDevicesStore((s) => s.updateTelemetry)
  const setDeviceStatus  = useDevicesStore((s) => s.setDeviceStatus)
  const addEvent         = useDevicesStore((s) => s.addEvent)
  const acknowledgeEvent = useDevicesStore((s) => s.acknowledgeEvent)
  const removeDevice     = useDevicesStore((s) => s.removeDevice)
  const operatorName     = useAuthStore((s) => s.operatorName)
  const logout           = useAuthStore((s) => s.logout)
  const navigate         = useNavigate()

  const [filter, setFilter]   = useState<FilterKey>('ALL')
  const [wsOk, setWsOk]       = useState(false)
  const [log, setLog]         = useState<LogEntry[]>([])
  const [clock, setClock]     = useState('')
  const logIdRef              = useRef(0)

  // Clock
  useEffect(() => {
    const tick = () => setClock(new Date().toLocaleTimeString('pt-BR'))
    tick()
    const id = setInterval(tick, 1000)
    return () => clearInterval(id)
  }, [])

  const pushLog = useCallback((type: string, src: string, msg: string) => {
    setLog((prev) => {
      const entry: LogEntry = { id: ++logIdRef.current, type, src, msg, ts: new Date().toISOString() }
      return [entry, ...prev].slice(0, 80)
    })
  }, [])

  const handleMessage = useCallback(
    (msg: Record<string, unknown>) => {
      const type = msg.type as string

      if (type === 'snapshot') {
        setSnapshot(
          (msg.devices as Device[]) ?? [],
          (msg.events  as DeviceEvent[]) ?? []
        )
        setWsOk(true)
        pushLog('system', 'Sistema', 'Snapshot recebido')
        return
      }

      if (type === 'device_status') {
        upsertDevice({
          device_id:    msg.device_id   as string,
          status:       msg.status      as Device['status'],
          ward_id:      (msg.ward_id    as string) ?? '',
          fw_version:   (msg.fw_version as string) ?? '',
          patient_name: (msg.patient_name as string) ?? null,
          room_label:   (msg.room_label   as string) ?? null,
        })
        const lbl = (msg.room_label as string) || (msg.device_id as string)
        pushLog('status', lbl, `Status: ${msg.status}`)
        return
      }

      if (type === 'telemetry') {
        const { device_id, type: _t, timestamp: _ts, ...rest } = msg
        updateTelemetry(device_id as string, rest as Partial<Device>)
        return
      }

      if (type === 'event') {
        const event: DeviceEvent = {
          id:           msg.event_id   as number,
          device_id:    msg.device_id  as string,
          event_type:   msg.event_type as DeviceEvent['event_type'],
          timestamp:    msg.timestamp  as string,
          acknowledged: 0,
        }
        addEvent(event)
        const dev = devices[event.device_id]
        const lbl = dev?.room_label || event.device_id
        if (event.event_type === 'EMERGENCY') {
          setDeviceStatus(event.device_id, 'EMERGENCY')
          playAlertBeep()
          pushLog('EMERGENCY', lbl, '🚨 Chamada de emergência!')
        } else {
          pushLog('REQUEST', lbl, '🔔 Chamado de paciente')
        }
        return
      }

      if (type === 'event_acknowledged') {
        acknowledgeEvent(msg.event_id as number)
        pushLog('ack', 'Sistema', `Evento #${msg.event_id} confirmado`)
        return
      }

      if (type === 'device_approved') {
        upsertDevice({ device_id: msg.device_id as string, approved: 1, status: 'OFFLINE' })
        pushLog('status', msg.device_id as string, 'Dispositivo aprovado')
        return
      }

      if (type === 'device_updated') {
        upsertDevice({
          device_id:    msg.device_id    as string,
          patient_name: (msg.patient_name as string) ?? null,
          room_label:   (msg.room_label   as string) ?? null,
        })
        return
      }

      if (type === 'device_removed') {
        removeDevice(msg.device_id as string)
        pushLog('status', msg.device_id as string, 'Dispositivo removido')
        return
      }
    },
    [setSnapshot, upsertDevice, updateTelemetry, setDeviceStatus, addEvent, acknowledgeEvent, removeDevice, pushLog, devices]
  )

  useWebSocket(handleMessage, 2000)

  // Mark WS ok on first snapshot
  useEffect(() => {
    if (Object.keys(devices).length > 0) setWsOk(true)
  }, [devices])

  const handleAcknowledge = async (deviceId: string) => {
    const event = events.find((e) => e.device_id === deviceId && !e.acknowledged)
    if (event) {
      await api.post(`/events/${event.id}/acknowledge`).catch(() => null)
      acknowledgeEvent(event.id)
      pushLog('ack', devices[deviceId]?.room_label || deviceId, '✅ Emergência confirmada')
    }
    setDeviceStatus(deviceId, 'ONLINE')
  }

  const deviceList       = Object.values(devices)
  const emergencyDevices = deviceList.filter((d) => d.status === 'EMERGENCY')
  const unackedEmg       = events.filter((e) => e.event_type === 'EMERGENCY' && !e.acknowledged)

  const counts = {
    online:    deviceList.filter((d) => d.status === 'ONLINE' || d.status === 'EMERGENCY').length,
    emergency: deviceList.filter((d) => d.status === 'EMERGENCY').length,
    offline:   deviceList.filter((d) => d.status === 'OFFLINE').length,
    pending:   deviceList.filter((d) => !d.approved).length,
    total:     deviceList.length,
  }

  const filteredDevices = deviceList.filter((d) => {
    if (filter === 'ALL')       return true
    if (filter === 'ONLINE')    return d.status === 'ONLINE' || d.status === 'EMERGENCY'
    if (filter === 'PENDING')   return !d.approved
    return d.status === filter
  })

  return (
    <div className="min-h-screen" style={{ background: 'var(--vk-bg)' }}>

      {/* ── Emergency banners ── */}
      {emergencyDevices.map((d) => (
        <div key={d.device_id}
          className="animate-banner-drop px-4 py-3 flex items-center justify-between text-sm font-semibold"
          style={{ background: '#7f1d1d', borderBottom: '1px solid #ef4444' }}>
          <span className="text-red-200">
            🚨 EMERGÊNCIA — <span className="text-white">{d.room_label || d.device_id}</span>
            {d.patient_name && <span className="ml-1 font-normal text-red-300">· {d.patient_name}</span>}
          </span>
          <button
            onClick={() => handleAcknowledge(d.device_id)}
            className="rounded-lg px-3 py-1 text-xs font-bold text-white hover:opacity-80 transition"
            style={{ background: '#ef4444' }}>
            Confirmar Atendimento
          </button>
        </div>
      ))}

      {/* ── Top bar ── */}
      <header
        className="sticky top-0 z-50 px-5 py-3 flex items-center justify-between gap-4"
        style={{ background: 'rgba(8,14,26,0.95)', backdropFilter: 'blur(14px)', borderBottom: '1px solid var(--vk-border)' }}>

        <div className="flex items-center gap-3">
          <span className="text-xl font-bold tracking-widest uppercase" style={{ color: 'var(--vk-accent)' }}>VitalKare</span>
          <span className="text-xs hidden sm:inline" style={{ color: 'var(--vk-muted)' }}>Monitor em Tempo Real</span>
        </div>

        <div className="flex items-center gap-3 text-xs">
          <span className="font-mono tabular-nums hidden sm:block" style={{ color: 'var(--vk-muted)' }}>{clock}</span>

          {/* WS indicator */}
          <div className="flex items-center gap-1.5 px-2.5 py-1.5 rounded-lg"
            style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)' }}>
            <span className={`w-2 h-2 rounded-full ${wsOk ? 'bg-green-400 animate-pulse' : 'bg-red-500'}`} />
            <span style={{ color: 'var(--vk-muted)' }}>{wsOk ? 'WebSocket' : 'Desconectado'}</span>
          </div>

          <button
            onClick={() => navigate('/admin')}
            className="px-3 py-1.5 rounded-lg text-xs font-medium transition hover:opacity-80"
            style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)', color: 'var(--vk-accent)' }}>
            ⚙ Admin
          </button>

          <span className="hidden sm:inline" style={{ color: 'var(--vk-muted)' }}>{operatorName}</span>

          <button
            onClick={() => { logout(); navigate('/login') }}
            className="px-3 py-1.5 rounded-lg text-xs transition hover:opacity-80"
            style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)', color: 'var(--vk-muted)' }}>
            Sair
          </button>
        </div>
      </header>

      {/* ── Stats ── */}
      <section className="max-w-7xl mx-auto px-5 mt-5 grid grid-cols-2 sm:grid-cols-5 gap-3">
        {[
          { label: 'Online',      value: counts.online,    color: '#00b2bd' },
          { label: 'Emergências', value: counts.emergency, color: '#ef4444' },
          { label: 'Offline',     value: counts.offline,   color: '#94a3b8' },
          { label: 'Pendentes',   value: counts.pending,   color: '#f59e0b' },
          { label: 'Total',       value: counts.total,     color: '#e2e8f0' },
        ].map((s) => (
          <div key={s.label} className="rounded-xl p-4"
            style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)' }}>
            <p className="text-[10px] font-semibold uppercase tracking-wider" style={{ color: 'var(--vk-muted)' }}>{s.label}</p>
            <p className="text-2xl font-bold mt-1 tabular-nums" style={{ color: s.color }}>{s.value}</p>
          </div>
        ))}
      </section>

      {/* ── Filter tabs ── */}
      <div className="max-w-7xl mx-auto px-5 mt-4 flex items-center gap-2 flex-wrap">
        {FILTERS.map((f) => (
          <button
            key={f.key}
            onClick={() => setFilter(f.key)}
            className="px-3 py-1.5 rounded-lg text-xs font-medium transition"
            style={filter === f.key
              ? { background: 'var(--vk-accent)', color: '#fff' }
              : { background: 'var(--vk-card)', border: '1px solid var(--vk-border)', color: 'var(--vk-muted)' }}>
            {f.label}
            {f.key !== 'ALL' && (() => {
              const cnt = f.key === 'ONLINE' ? counts.online
                        : f.key === 'EMERGENCY' ? counts.emergency
                        : f.key === 'OFFLINE' ? counts.offline
                        : counts.pending
              return cnt > 0 ? <span className="ml-1.5 px-1 py-0.5 rounded bg-white/20 text-[10px]">{cnt}</span> : null
            })()}
          </button>
        ))}
      </div>

      {/* ── Main content ── */}
      <main className="max-w-7xl mx-auto px-5 mt-4 pb-10 grid grid-cols-1 lg:grid-cols-[1fr_300px] gap-5 items-start">

        {/* Device grid */}
        <div>
          {filteredDevices.length === 0 ? (
            <div className="rounded-xl p-10 text-center"
              style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)' }}>
              <p className="text-4xl mb-3">📡</p>
              <p className="font-medium" style={{ color: 'var(--vk-text)' }}>Nenhum dispositivo neste filtro.</p>
              <p className="text-sm mt-1" style={{ color: 'var(--vk-muted)' }}>
                Conecte uma Kiira ou{' '}
                <button onClick={() => navigate('/admin')} style={{ color: 'var(--vk-accent)' }} className="hover:underline">
                  configure comissionamentos
                </button>.
              </p>
            </div>
          ) : (
            <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-3 gap-4">
              {filteredDevices.map((d) => (
                <DeviceCard
                  key={d.device_id}
                  device={d}
                  onAcknowledge={d.status === 'EMERGENCY' ? handleAcknowledge : undefined}
                />
              ))}
            </div>
          )}
        </div>

        {/* Event log */}
        <div className="rounded-xl overflow-hidden lg:sticky lg:top-20"
          style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)' }}>
          <div className="flex items-center justify-between px-4 py-3"
            style={{ borderBottom: '1px solid var(--vk-border)' }}>
            <span className="text-xs font-semibold uppercase tracking-wider" style={{ color: 'var(--vk-muted)' }}>
              Log de Eventos
            </span>
            <button onClick={() => setLog([])}
              className="text-[10px] hover:opacity-80 transition"
              style={{ color: 'var(--vk-muted)' }}>
              Limpar
            </button>
          </div>
          <div className="overflow-y-auto" style={{ maxHeight: '70vh' }}>
            {log.length === 0 ? (
              <p className="text-xs text-center py-6" style={{ color: 'var(--vk-muted)' }}>
                Aguardando eventos...
              </p>
            ) : (
              log.map((entry) => (
                <div key={entry.id} className="animate-log-in flex flex-col gap-0.5 px-4 py-2.5"
                  style={{ borderBottom: '1px solid var(--vk-border)' }}>
                  <div className="flex items-center justify-between gap-2">
                    <span
                      className="text-[10px] font-semibold px-1.5 py-0.5 rounded"
                      style={entry.type === 'EMERGENCY' ? { background: '#450a0a', color: '#fca5a5' }
                           : entry.type === 'REQUEST'   ? { background: '#0c1a2e', color: '#67e8f9' }
                           : entry.type === 'ack'       ? { background: '#052e16', color: '#4ade80' }
                           :                              { background: '#1e293b', color: '#94a3b8' }}>
                      {entry.type}
                    </span>
                    <span className="text-[10px]" style={{ color: 'var(--vk-muted)' }}>{relTime(entry.ts)}</span>
                  </div>
                  <p className="text-xs font-medium" style={{ color: 'var(--vk-text)' }}>{entry.src}</p>
                  <p className="text-[11px]" style={{ color: 'var(--vk-muted)' }}>{entry.msg}</p>
                </div>
              ))
            )}
          </div>
        </div>

      </main>
    </div>
  )
}
