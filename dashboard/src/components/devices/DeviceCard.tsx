import type { Device } from '@/store/devicesStore'

interface DeviceCardProps {
  device: Device
  onAcknowledge?: (deviceId: string) => void
}

const STATUS_META: Record<string, { border: string; badgeBg: string; badgeText: string; label: string }> = {
  ONLINE:    { border: '#00b2bd', badgeBg: '#0d4d52', badgeText: '#67e8f9', label: 'Online'      },
  EMERGENCY: { border: '#ef4444', badgeBg: '#450a0a', badgeText: '#fca5a5', label: 'Emergência'  },
  OFFLINE:   { border: '#94a3b8', badgeBg: '#1e293b', badgeText: '#94a3b8', label: 'Offline'     },
  PENDING:   { border: '#f59e0b', badgeBg: '#422006', badgeText: '#fcd34d', label: 'Pendente'    },
}

const AVATAR_COLORS = ['#0891b2','#0d9488','#7c3aed','#db2777','#d97706','#059669']

function avatarBg(name: string) {
  return AVATAR_COLORS[name.charCodeAt(0) % AVATAR_COLORS.length]
}

function initials(name: string) {
  return name.split(' ').slice(0, 2).map((n) => n[0]).join('').toUpperCase()
}

function uptime(ms: number | null): string | null {
  if (ms == null) return null
  const s = Math.floor(ms / 1000)
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  return h > 0 ? `${h}h ${m}m` : `${m}m`
}

function signalStrength(rssi: number | null): number {
  if (rssi == null) return 0
  if (rssi >= -55) return 4
  if (rssi >= -67) return 3
  if (rssi >= -78) return 2
  return 1
}

function SignalBars({ rssi }: { rssi: number | null }) {
  const level = signalStrength(rssi)
  return (
    <svg width="18" height="14" viewBox="0 0 18 14" style={{ display: 'inline-block', verticalAlign: 'middle' }}>
      {[1,2,3,4].map((b) => (
        <rect
          key={b}
          x={(b - 1) * 5}
          y={14 - b * 3}
          width="3"
          height={b * 3}
          rx="1"
          fill={b <= level ? '#00b2bd' : '#334155'}
        />
      ))}
    </svg>
  )
}

export function DeviceCard({ device, onAcknowledge }: DeviceCardProps) {
  const meta = STATUS_META[device.status] ?? STATUS_META.OFFLINE
  const isEmergency = device.status === 'EMERGENCY'
  const isOffline   = device.status === 'OFFLINE'
  const isPending   = device.status === 'PENDING'

  const displayName = device.room_label || device.device_id
  const up = uptime(device.uptime_ms)

  return (
    <div
      className={`rounded-2xl overflow-hidden transition-transform hover:-translate-y-0.5 hover:shadow-xl cursor-pointer ${isEmergency ? 'animate-emg' : ''}`}
      style={{
        background: '#ffffff',
        borderTop: `4px solid ${meta.border}`,
        boxShadow: `0 2px 16px rgba(0,0,0,.35)`,
        opacity: isOffline ? 0.7 : 1,
      }}>

      {/* Header */}
      <div className="px-4 pt-4 pb-3 flex items-start justify-between gap-2">
        <div className="flex items-center gap-2.5 min-w-0">
          {device.patient_name ? (
            <div
              className="w-9 h-9 rounded-full flex-shrink-0 flex items-center justify-center text-xs font-bold text-white"
              style={{ background: avatarBg(device.patient_name) }}>
              {initials(device.patient_name)}
            </div>
          ) : (
            <div className="w-9 h-9 rounded-full flex-shrink-0 flex items-center justify-center text-lg"
              style={{ background: '#f1f5f9' }}>
              {isPending ? '⏳' : isOffline ? '📴' : '📡'}
            </div>
          )}
          <div className="min-w-0">
            <p className="font-bold text-gray-800 text-sm leading-tight truncate">{displayName}</p>
            {device.patient_name && (
              <p className="text-xs text-gray-500 truncate">{device.patient_name}</p>
            )}
            {!device.patient_name && (
              <p className="text-xs text-gray-400 truncate font-mono">{device.device_id}</p>
            )}
          </div>
        </div>

        <span
          className="flex-shrink-0 text-[10px] font-semibold px-2 py-0.5 rounded-full"
          style={{ background: meta.badgeBg, color: meta.badgeText }}>
          {meta.label}
        </span>
      </div>

      {/* Vitals */}
      {!isOffline && !isPending && (device.bpm != null || device.spo2_pct != null) && (
        <div className="px-4 pb-3 flex gap-4">
          {device.bpm != null && (
            <div className="text-center">
              <p className="text-xl font-bold text-gray-800 leading-none">{device.bpm}</p>
              <p className="text-[10px] text-gray-400 mt-0.5">BPM ❤️</p>
            </div>
          )}
          {device.spo2_pct != null && device.spo2_pct > 0 && (
            <div className="text-center">
              <p className="text-xl font-bold text-gray-800 leading-none">{device.spo2_pct}%</p>
              <p className="text-[10px] text-gray-400 mt-0.5">SpO₂ 🩸</p>
            </div>
          )}
        </div>
      )}

      {/* Environment */}
      {!isOffline && !isPending && (device.temperature_c != null || device.humidity_pct != null) && (
        <div className="px-4 pb-3 flex gap-4 text-xs text-gray-600">
          {device.temperature_c != null && (
            <span>🌡 <b>{device.temperature_c.toFixed(1)}°C</b></span>
          )}
          {device.humidity_pct != null && (
            <span>💧 <b>{device.humidity_pct.toFixed(0)}%</b></span>
          )}
          {device.light_lux != null && (
            <span>💡 <b>{device.light_lux}</b> lux</span>
          )}
        </div>
      )}

      {/* Footer */}
      <div className="px-4 py-2.5 flex items-center justify-between gap-2"
        style={{ borderTop: '1px solid #f1f5f9' }}>
        <div className="flex items-center gap-2 text-[11px] text-gray-400">
          <SignalBars rssi={device.rssi} />
          {device.rssi != null && <span>{device.rssi} dBm</span>}
        </div>
        <div className="flex items-center gap-2 text-[11px] text-gray-400">
          {up && <span>⏱ {up}</span>}
          {device.ward_id && <span className="truncate max-w-[80px]">{device.ward_id}</span>}
        </div>
      </div>

      {/* Emergency button */}
      {isEmergency && onAcknowledge && (
        <div className="px-4 pb-4">
          <button
            onClick={() => onAcknowledge(device.device_id)}
            className="w-full rounded-xl py-2 text-xs font-bold text-white transition hover:opacity-90"
            style={{ background: '#ef4444' }}>
            ✓ Confirmar Atendimento
          </button>
        </div>
      )}

      {/* Pending notice */}
      {isPending && (
        <div className="px-4 pb-4">
          <p className="text-[11px] text-center font-medium" style={{ color: '#b45309' }}>
            Aguarda aprovação no painel admin
          </p>
        </div>
      )}
    </div>
  )
}
