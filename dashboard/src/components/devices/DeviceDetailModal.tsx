import type { ReactNode } from 'react'
import type { Device, DeviceEvent } from '@/store/devicesStore'

interface DeviceDetailModalProps {
  device: Device
  events: DeviceEvent[]
  onClose: () => void
  onAcknowledge?: (deviceId: string) => void
}

const STATUS_COLOR: Record<string, string> = {
  ONLINE:    '#00b2bd',
  EMERGENCY: '#ef4444',
  OFFLINE:   '#94a3b8',
  PENDING:   '#f59e0b',
}

function Section({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="mb-5">
      <p className="text-[10px] font-semibold uppercase tracking-widest mb-2"
        style={{ color: 'var(--vk-muted)' }}>{title}</p>
      {children}
    </div>
  )
}

function Metric({ label, value, unit }: { label: string; value: string | number | null; unit?: string }) {
  if (value == null) return null
  return (
    <div className="flex flex-col items-center p-2.5 rounded-xl"
      style={{ background: 'var(--vk-bg)', border: '1px solid var(--vk-border)' }}>
      <span className="text-xl font-bold tabular-nums" style={{ color: 'var(--vk-text)' }}>
        {typeof value === 'number' ? value.toLocaleString() : value}
        {unit && <span className="text-xs ml-0.5 font-normal" style={{ color: 'var(--vk-muted)' }}>{unit}</span>}
      </span>
      <span className="text-[10px] mt-0.5 text-center" style={{ color: 'var(--vk-muted)' }}>{label}</span>
    </div>
  )
}

function Row({ label, value }: { label: string; value: string | number | null }) {
  if (value == null || value === '') return null
  return (
    <div className="flex justify-between items-center py-1.5"
      style={{ borderBottom: '1px solid var(--vk-border)' }}>
      <span className="text-xs" style={{ color: 'var(--vk-muted)' }}>{label}</span>
      <span className="text-xs font-medium font-mono" style={{ color: 'var(--vk-text)' }}>{value}</span>
    </div>
  )
}

function uptimeLabel(ms: number | null): string | null {
  if (ms == null) return null
  const s = Math.floor(ms / 1000)
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  return h > 0 ? `${h}h ${m}m` : `${m}m`
}

function relTime(iso: string): string {
  const s = Math.round((Date.now() - new Date(iso).getTime()) / 1000)
  if (s < 5)   return 'agora'
  if (s < 60)  return `há ${s}s`
  if (s < 3600) return `há ${Math.floor(s / 60)}m`
  return `há ${Math.floor(s / 3600)}h`
}

export function DeviceDetailModal({ device, events, onClose, onAcknowledge }: DeviceDetailModalProps) {
  const deviceEvents = events
    .filter((e) => e.device_id === device.device_id)
    .slice(0, 10)

  const accentColor = STATUS_COLOR[device.status] ?? STATUS_COLOR.OFFLINE
  const uptime = uptimeLabel(device.uptime_ms)
  const hasVitals = device.bpm != null || device.spo2_pct != null
  const hasEnv = device.temperature_c != null || device.humidity_pct != null || device.light_lux != null

  return (
    <div
      className="fixed inset-0 z-50 flex items-start justify-end"
      style={{ background: 'rgba(0,0,0,0.6)', backdropFilter: 'blur(4px)' }}
      onClick={(e) => { if (e.target === e.currentTarget) onClose() }}>

      {/* Panel */}
      <div
        className="h-full overflow-y-auto w-full max-w-md shadow-2xl flex flex-col"
        style={{ background: 'var(--vk-card)', borderLeft: `3px solid ${accentColor}` }}>

        {/* Header */}
        <div className="sticky top-0 z-10 px-5 py-4 flex items-start justify-between gap-3"
          style={{ background: 'var(--vk-card)', borderBottom: '1px solid var(--vk-border)' }}>
          <div className="min-w-0">
            <div className="flex items-center gap-2 flex-wrap">
              <h2 className="text-base font-bold truncate" style={{ color: 'var(--vk-text)' }}>
                {device.room_label || device.device_id}
              </h2>
              <span className="text-[10px] font-semibold px-2 py-0.5 rounded-full flex-shrink-0"
                style={{ background: accentColor + '22', color: accentColor, border: `1px solid ${accentColor}55` }}>
                {device.status}
              </span>
            </div>
            {device.patient_name && (
              <p className="text-sm mt-0.5 truncate" style={{ color: 'var(--vk-muted)' }}>
                👤 {device.patient_name}
              </p>
            )}
          </div>
          <button
            onClick={onClose}
            className="flex-shrink-0 w-8 h-8 flex items-center justify-center rounded-lg text-lg hover:opacity-70 transition"
            style={{ background: 'var(--vk-bg)', color: 'var(--vk-muted)' }}>
            ✕
          </button>
        </div>

        {/* Body */}
        <div className="px-5 py-4 flex-1">

          {/* Emergency CTA */}
          {device.status === 'EMERGENCY' && onAcknowledge && (
            <button
              onClick={() => onAcknowledge(device.device_id)}
              className="w-full mb-5 rounded-xl py-3 text-sm font-bold text-white transition hover:opacity-90"
              style={{ background: '#ef4444' }}>
              🚨 Confirmar Atendimento de Emergência
            </button>
          )}

          {/* Vitals */}
          {hasVitals && (
            <Section title="Sinais Vitais">
              <div className="grid grid-cols-2 gap-2">
                <Metric label="Freq. Cardíaca" value={device.bpm} unit=" BPM" />
                <Metric label="SpO₂" value={device.spo2_pct != null && device.spo2_pct > 0 ? device.spo2_pct : null} unit="%" />
              </div>
              {device.finger_detected === 0 && (
                <p className="text-[11px] mt-2 text-center" style={{ color: 'var(--vk-muted)' }}>
                  Sem dedo detectado no oxímetro
                </p>
              )}
            </Section>
          )}

          {/* Environment */}
          {hasEnv && (
            <Section title="Ambiente">
              <div className="grid grid-cols-3 gap-2">
                <Metric label="Temperatura" value={device.temperature_c != null ? +device.temperature_c.toFixed(1) : null} unit="°C" />
                <Metric label="Umidade" value={device.humidity_pct != null ? +device.humidity_pct.toFixed(0) : null} unit="%" />
                <Metric label="Luminosidade" value={device.light_lux} unit=" lux" />
              </div>
            </Section>
          )}

          {/* System */}
          <Section title="Sistema">
            <div className="rounded-xl overflow-hidden" style={{ border: '1px solid var(--vk-border)' }}>
              <Row label="Device ID"   value={device.device_id} />
              <Row label="Ala / Ward"  value={device.ward_id || null} />
              <Row label="Firmware"    value={device.fw_version || null} />
              <Row label="Sinal WiFi"  value={device.rssi != null ? `${device.rssi} dBm` : null} />
              <Row label="Heap Livre"  value={device.free_heap_bytes != null ? `${Math.round(device.free_heap_bytes / 1024)} KB` : null} />
              <Row label="Uptime"      value={uptime} />
              <Row label="Última atualização" value={
                device.last_seen != null
                  ? new Date(device.last_seen * 1000).toLocaleTimeString('pt-BR')
                  : null
              } />
            </div>
          </Section>

          {/* Events */}
          <Section title={`Eventos Recentes (${deviceEvents.length})`}>
            {deviceEvents.length === 0 ? (
              <p className="text-xs text-center py-4" style={{ color: 'var(--vk-muted)' }}>
                Nenhum evento registrado para este dispositivo.
              </p>
            ) : (
              <div className="space-y-2">
                {deviceEvents.map((ev) => (
                  <div key={ev.id}
                    className="flex items-center justify-between gap-2 rounded-lg px-3 py-2"
                    style={{ background: 'var(--vk-bg)', border: '1px solid var(--vk-border)' }}>
                    <div className="flex items-center gap-2 min-w-0">
                      <span
                        className="text-[10px] font-semibold px-1.5 py-0.5 rounded flex-shrink-0"
                        style={ev.event_type === 'EMERGENCY'
                          ? { background: '#450a0a', color: '#fca5a5' }
                          : { background: '#0c1a2e', color: '#67e8f9' }}>
                        {ev.event_type}
                      </span>
                      <span className="text-xs truncate" style={{ color: 'var(--vk-muted)' }}>
                        {relTime(ev.timestamp)}
                      </span>
                    </div>
                    {ev.acknowledged ? (
                      <span className="text-[10px] font-semibold flex-shrink-0"
                        style={{ color: '#4ade80' }}>✓ ACK</span>
                    ) : (
                      <span className="text-[10px] font-semibold flex-shrink-0"
                        style={{ color: '#fcd34d' }}>Pendente</span>
                    )}
                  </div>
                ))}
              </div>
            )}
          </Section>

          {/* Noise */}
          {device.noise_db != null && (
            <Section title="Nível de Ruído">
              <div className="rounded-xl p-3 text-center"
                style={{ background: 'var(--vk-bg)', border: '1px solid var(--vk-border)' }}>
                <span className="text-2xl font-bold tabular-nums" style={{ color: 'var(--vk-text)' }}>
                  {device.noise_db}
                </span>
                <span className="text-xs ml-1" style={{ color: 'var(--vk-muted)' }}>dB</span>
              </div>
            </Section>
          )}

        </div>

        {/* Footer */}
        <div className="px-5 py-3 flex justify-end"
          style={{ borderTop: '1px solid var(--vk-border)', background: 'var(--vk-card)' }}>
          <button
            onClick={onClose}
            className="rounded-lg px-4 py-2 text-sm hover:opacity-80 transition"
            style={{ background: 'var(--vk-bg)', border: '1px solid var(--vk-border)', color: 'var(--vk-muted)' }}>
            Fechar
          </button>
        </div>
      </div>
    </div>
  )
}
