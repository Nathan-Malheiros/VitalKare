/**
 * PatientModal — Modal de detalhes do paciente / leito
 *
 * Abre ao clicar em um BedCard com status ONLINE, OCCUPIED ou EMERGENCY.
 * Busca histório de eventos e gravações de áudio da API REST.
 * Estilo Shadcn/UI: backdrop-blur, bordas suaves, sticky header.
 */

import { useEffect } from 'react'
import { useQuery } from '@tanstack/react-query'
import type { Bed } from '@/store/bedsStore'
import api from '@/services/api'

// =========================================================
// Tipos locais
// =========================================================
interface BedEventItem {
  id: string
  event_type: string
  priority: number
  status: string
  payload: Record<string, unknown> | null
  created_at: string
}

interface AudioTranscriptItem {
  id: string
  text: string | null
  confidence: number | null
  language: string | null
  model_used: string | null
}

interface AudioRecordItem {
  id: string
  duration_seconds: number | null
  file_size_bytes: number | null
  created_at: string
  transcriptions: AudioTranscriptItem[]
}

// =========================================================
// Constantes de estilo
// =========================================================
const STATUS_BADGE: Record<string, string> = {
  ONLINE:           'border-green-300 bg-green-50 text-green-700',
  OCCUPIED:         'border-blue-300 bg-blue-50 text-blue-700',
  EMERGENCY:        'border-red-400 bg-red-100 text-red-700',
  CRITICAL_OFFLINE: 'border-red-300 bg-red-50 text-red-600',
  OFFLINE:          'border-gray-300 bg-gray-50 text-gray-500',
  FREE:             'border-gray-200 bg-white text-gray-400',
}

const STATUS_LABEL: Record<string, string> = {
  ONLINE:           'Online',
  OCCUPIED:         'Ocupado',
  EMERGENCY:        '⚠ Emergência',
  CRITICAL_OFFLINE: 'Crítico / Offline',
  OFFLINE:          'Offline',
  FREE:             'Livre',
}

const EVENT_TYPE_STYLE: Record<string, { label: string; bg: string; color: string }> = {
  EMERGENCY:     { label: 'Emergência',  bg: '#fef2f2', color: '#dc2626' },
  REQUEST:       { label: 'Solicitação', bg: '#eff6ff', color: '#2563eb' },
  AUDIO:         { label: 'Áudio',       bg: '#faf5ff', color: '#7c3aed' },
  ENVIRONMENTAL: { label: 'Ambiente',    bg: '#f0fdf4', color: '#16a34a' },
  SYSTEM:        { label: 'Sistema',     bg: '#f8fafc', color: '#64748b' },
}

// =========================================================
// Helpers
// =========================================================
function initials(name: string): string {
  return name.split(' ').slice(0, 2).map((w) => w[0]).join('').toUpperCase()
}

function avatarColor(name: string): string {
  const COLORS = ['#3b82f6', '#8b5cf6', '#ec4899', '#f59e0b', '#10b981', '#06b6d4', '#6366f1']
  let hash = 0
  for (let i = 0; i < name.length; i++) hash = name.charCodeAt(i) + ((hash << 5) - hash)
  return COLORS[Math.abs(hash) % COLORS.length]
}

function fmtTime(iso: string): string {
  return new Date(iso).toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' })
}

function fmtDate(iso: string): string {
  return new Date(iso).toLocaleDateString('pt-BR', { day: '2-digit', month: 'short' })
}

// =========================================================
// Sub-componente: card de vital
// =========================================================
function VitalCard({ icon, label, value, unit }: {
  icon: string; label: string; value: string | number | null | undefined; unit?: string
}) {
  return (
    <div
      className="rounded-xl p-3 flex flex-col gap-1"
      style={{ border: '1px solid #e2e8f0', background: '#f8fafc' }}
    >
      <span className="text-base">{icon}</span>
      <span className="text-[10px] font-medium text-slate-400 uppercase tracking-wide">{label}</span>
      <span className="text-sm font-bold text-slate-800">
        {value != null ? `${value}${unit ? ` ${unit}` : ''}` : '—'}
      </span>
    </div>
  )
}

// =========================================================
// Sub-componente: linha de evento
// =========================================================
function EventRow({ event }: { event: BedEventItem }) {
  const style = EVENT_TYPE_STYLE[event.event_type] ?? EVENT_TYPE_STYLE.SYSTEM
  return (
    <div className="flex items-start gap-3 py-2.5" style={{ borderBottom: '1px solid #f1f5f9' }}>
      <div className="flex-shrink-0 mt-0.5">
        <span
          className="text-[9px] font-bold px-2 py-0.5 rounded-full"
          style={{ background: style.bg, color: style.color, border: `1px solid ${style.color}33` }}
        >
          {style.label}
        </span>
      </div>
      <div className="flex-1 min-w-0">
        <p className="text-xs text-slate-600 truncate">
          {event.payload?.message as string
            ?? event.payload?.trigger as string
            ?? event.event_type}
        </p>
        <p className="text-[10px] text-slate-400 mt-0.5">
          {fmtDate(event.created_at)} · {fmtTime(event.created_at)}
          {event.status === 'ACKNOWLEDGED' && (
            <span className="ml-1.5 text-green-600 font-medium">✓ Atendido</span>
          )}
        </p>
      </div>
    </div>
  )
}

// =========================================================
// Sub-componente: card de gravação de áudio
// =========================================================
function AudioCard({ record }: { record: AudioRecordItem }) {
  const transcript = record.transcriptions?.[0]
  return (
    <div
      className="rounded-xl p-3"
      style={{ background: '#faf5ff', border: '1px solid #e9d5ff' }}
    >
      <div className="flex items-center justify-between mb-1">
        <span className="text-xs font-medium text-purple-700">
          🎙 {record.duration_seconds ? `${record.duration_seconds}s` : 'Áudio'}
        </span>
        <span className="text-[10px] text-purple-400">
          {fmtDate(record.created_at)} · {fmtTime(record.created_at)}
        </span>
      </div>
      {transcript?.text ? (
        <p className="text-xs text-slate-700 italic">"{transcript.text}"</p>
      ) : (
        <p className="text-[11px] text-slate-400 italic">
          Transcrição pendente (Whisper — Fase 4)
        </p>
      )}
      {transcript?.confidence != null && (
        <p className="text-[10px] text-purple-400 mt-0.5">
          Confiança: {(transcript.confidence * 100).toFixed(0)}%
        </p>
      )}
    </div>
  )
}

// =========================================================
// Sub-componente: estado vazio (leito livre/offline)
// =========================================================
function EmptyState({ bed }: { bed: Bed }) {
  const msgs: Record<string, string> = {
    FREE:             'Leito livre — nenhum paciente admitido.',
    OFFLINE:          'Dispositivo KIIRA V2 sem comunicação.',
    CRITICAL_OFFLINE: 'Dispositivo offline há mais de 30 min. Verificar hardware.',
  }
  return (
    <div className="flex flex-col items-center justify-center py-10 text-slate-400 gap-2">
      <span className="text-3xl">{bed.status === 'FREE' ? '🛏️' : '📡'}</span>
      <p className="text-sm text-center">{msgs[bed.status] ?? 'Sem dados disponíveis.'}</p>
    </div>
  )
}

// =========================================================
// Componente principal
// =========================================================
interface PatientModalProps {
  bed: Bed | null
  onClose: () => void
}

export function PatientModal({ bed, onClose }: PatientModalProps) {
  // Fecha com Escape
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose() }
    document.addEventListener('keydown', onKey)
    return () => document.removeEventListener('keydown', onKey)
  }, [onClose])

  // Busca eventos do leito
  const { data: events = [] } = useQuery<BedEventItem[]>({
    queryKey: ['bed-events', bed?.id],
    queryFn: () => api.get(`/beds/${bed!.id}/events?limit=8`).then((r) => r.data),
    enabled: !!bed && !['FREE', 'OFFLINE', 'CRITICAL_OFFLINE'].includes(bed.status),
    staleTime: 15_000,
  })

  // Busca gravações de áudio do leito
  const { data: audioRecords = [] } = useQuery<AudioRecordItem[]>({
    queryKey: ['bed-audio', bed?.id],
    queryFn: () => api.get(`/beds/${bed!.id}/audio?limit=5`).then((r) => r.data),
    enabled: !!bed && !['FREE', 'OFFLINE', 'CRITICAL_OFFLINE'].includes(bed.status),
    staleTime: 15_000,
  })

  if (!bed) return null

  const isFreeOrOffline = ['FREE', 'OFFLINE', 'CRITICAL_OFFLINE'].includes(bed.status)
  const posture = bed.posture_label ?? (bed.motion_detected ? 'Movimento Detectado' : null)

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center p-4"
      style={{ background: 'rgba(15,23,42,.58)', backdropFilter: 'blur(6px)' }}
      onClick={(e) => { if (e.target === e.currentTarget) onClose() }}
    >
      <div
        className="w-full max-w-lg rounded-2xl shadow-2xl flex flex-col bg-white"
        style={{ border: '1px solid #e2e8f0', maxHeight: '88vh' }}
        onClick={(e) => e.stopPropagation()}
      >
        {/* ── Sticky header ── */}
        <div
          className="px-6 pt-5 pb-4 flex items-start justify-between gap-3 flex-shrink-0 bg-white rounded-t-2xl"
          style={{ borderBottom: '1px solid #e2e8f0', position: 'sticky', top: 0, zIndex: 10 }}
        >
          <div className="flex items-center gap-3 min-w-0">
            {/* Avatar */}
            <div
              className="w-11 h-11 rounded-full flex-shrink-0 flex items-center justify-center text-sm font-bold text-white"
              style={{ background: bed.patient_name ? avatarColor(bed.patient_name) : '#94a3b8' }}
            >
              {bed.patient_name ? initials(bed.patient_name) : bed.bed_number}
            </div>
            <div className="min-w-0">
              <p className="font-bold text-slate-900 text-base leading-tight truncate">
                {bed.patient_name ?? (bed.status === 'FREE' ? 'Leito Disponível' : 'Sem Comunicação')}
              </p>
              <p className="text-[11px] text-slate-400 mt-0.5 font-mono">
                KIIRA-V2 · Leito {bed.bed_number}
              </p>
            </div>
          </div>
          <div className="flex items-center gap-2 flex-shrink-0 mt-0.5">
            <span className={`text-[9px] font-bold px-2.5 py-1 rounded-full border ${STATUS_BADGE[bed.status] ?? STATUS_BADGE.OFFLINE}`}>
              {STATUS_LABEL[bed.status] ?? bed.status}
            </span>
            <button
              onClick={onClose}
              className="w-7 h-7 rounded-lg flex items-center justify-center text-xs font-bold text-slate-400 hover:text-slate-700 hover:bg-slate-100 transition"
              style={{ border: '1px solid #e2e8f0' }}
              aria-label="Fechar modal"
            >
              ✕
            </button>
          </div>
        </div>

        {/* ── Scrollable body ── */}
        <div className="overflow-y-auto px-6 py-5 space-y-5 flex-1">
          {isFreeOrOffline ? (
            <EmptyState bed={bed} />
          ) : (
            <>
              {/* Vitais 2×2 */}
              <section>
                <h3 className="text-xs font-semibold text-slate-500 uppercase tracking-wider mb-2">
                  Sinais Ambientais
                </h3>
                <div className="grid grid-cols-2 gap-2">
                  <VitalCard icon="🌡" label="Temperatura" value={bed.temperature_celsius?.toFixed(1)} unit="°C" />
                  <VitalCard icon="💧" label="Umidade" value={bed.humidity_percent?.toFixed(0)} unit="%" />
                  <VitalCard icon="💡" label="Luminosidade" value={bed.luminosity_lux?.toFixed(0)} unit="lux" />
                  <VitalCard icon="🔄" label="Postura (MPU6050)" value={posture} />
                </div>
              </section>

              {/* Histórico de eventos */}
              <section>
                <h3 className="text-xs font-semibold text-slate-500 uppercase tracking-wider mb-1">
                  Histórico de Eventos
                </h3>
                {events.length === 0 ? (
                  <p className="text-xs text-slate-400 py-3">Nenhum evento registrado.</p>
                ) : (
                  <div>
                    {events.map((e) => <EventRow key={e.id} event={e} />)}
                  </div>
                )}
              </section>

              {/* Gravações de áudio */}
              <section>
                <h3 className="text-xs font-semibold text-slate-500 uppercase tracking-wider mb-2">
                  Gravações de Áudio
                </h3>
                {audioRecords.length === 0 ? (
                  <p className="text-xs text-slate-400">Nenhuma gravação recente.</p>
                ) : (
                  <div className="space-y-2">
                    {audioRecords.map((r) => <AudioCard key={r.id} record={r} />)}
                  </div>
                )}
              </section>
            </>
          )}
        </div>
      </div>
    </div>
  )
}
