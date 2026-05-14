import { BedCard } from './BedCard'
import { useBedsStore } from '@/store/bedsStore'

interface BedGridProps {
  onAcknowledge?: (bedId: string) => void
  onBedClick?: (bedId: string) => void
}

export function BedGrid({ onAcknowledge, onBedClick }: BedGridProps) {
  const beds = useBedsStore((s) => Object.values(s.beds))

  // Emergências primeiro
  const sorted = [...beds].sort((a, b) => {
    const priority = { EMERGENCY: 0, CRITICAL_OFFLINE: 1, OCCUPIED: 2, ONLINE: 3, FREE: 4, OFFLINE: 5 }
    return (priority[a.status] ?? 9) - (priority[b.status] ?? 9)
  })

  if (sorted.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center h-64 text-gray-400">
        <p className="text-2xl mb-2">🏥</p>
        <p>Nenhum leito cadastrado.</p>
      </div>
    )
  }

  return (
    <div className="grid grid-cols-2 gap-4 sm:grid-cols-3 lg:grid-cols-4 xl:grid-cols-5">
      {sorted.map((bed) => (
        <BedCard key={bed.id} bed={bed} onAcknowledge={onAcknowledge} onClick={onBedClick} />
      ))}
    </div>
  )
}
