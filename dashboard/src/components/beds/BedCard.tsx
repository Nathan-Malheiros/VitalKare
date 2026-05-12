import clsx from 'clsx'
import type { Bed } from '@/store/bedsStore'

interface BedCardProps {
  bed: Bed
  onAcknowledge?: (bedId: string) => void
}

const STATUS_STYLES: Record<string, string> = {
  ONLINE:           'border-green-400 bg-green-50',
  OCCUPIED:         'border-blue-400 bg-blue-50',
  EMERGENCY:        'border-red-600 bg-red-100 animate-pulse ring-2 ring-red-500',
  CRITICAL_OFFLINE: 'border-red-400 bg-red-50',
  OFFLINE:          'border-gray-300 bg-gray-50 opacity-60',
  FREE:             'border-dashed border-gray-300 bg-white',
}

const STATUS_LABEL: Record<string, string> = {
  ONLINE:           'Online',
  OCCUPIED:         'Ocupado',
  EMERGENCY:        '⚠ Emergência',
  CRITICAL_OFFLINE: 'Crítico / Offline',
  OFFLINE:          'Offline',
  FREE:             'Livre',
}

export function BedCard({ bed, onAcknowledge }: BedCardProps) {
  const styles = STATUS_STYLES[bed.status] ?? STATUS_STYLES.OFFLINE

  return (
    <div className={clsx('rounded-xl border-2 p-4 shadow-sm transition-all', styles)}>
      {/* Cabeçalho */}
      <div className="flex items-center justify-between mb-2">
        <span className="text-lg font-bold text-gray-800">Leito {bed.bed_number}</span>
        <span className={clsx(
          'text-xs font-semibold px-2 py-0.5 rounded-full',
          bed.status === 'EMERGENCY' ? 'bg-red-600 text-white' : 'bg-white text-gray-600'
        )}>
          {STATUS_LABEL[bed.status] ?? bed.status}
        </span>
      </div>

      {/* Paciente */}
      {bed.patient_name && (
        <p className="text-sm text-gray-700 mb-1 truncate">
          <span className="font-medium">Paciente:</span> {bed.patient_name}
        </p>
      )}

      {/* Ambiente */}
      {bed.temperature_celsius != null && (
        <p className="text-xs text-gray-500">
          🌡 {bed.temperature_celsius.toFixed(1)}°C &nbsp;
          💧 {bed.humidity_percent?.toFixed(0)}%
        </p>
      )}

      {/* Último heartbeat */}
      {bed.last_heartbeat && (
        <p className="text-xs text-gray-400 mt-1">
          Último contato: {new Date(bed.last_heartbeat).toLocaleTimeString('pt-BR')}
        </p>
      )}

      {/* Botão de atendimento */}
      {bed.status === 'EMERGENCY' && onAcknowledge && (
        <button
          onClick={() => onAcknowledge(bed.bed_number)}
          className="mt-3 w-full rounded-lg bg-red-600 py-1.5 text-sm font-semibold text-white hover:bg-red-700 transition"
        >
          Confirmar Atendimento
        </button>
      )}
    </div>
  )
}
