import { useState, useEffect } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { useNavigate } from 'react-router-dom'
import { useAuthStore } from '@/store/authStore'
import api from '@/services/api'
import type { Device } from '@/store/devicesStore'

// ── Types ─────────────────────────────────────────────────────────
interface EditDeviceModal {
  device_id: string
  patient_name: string
  room_label: string
}

interface ProvisionedSlot {
  id: number
  bed_label: string
  expected_ip: string
  ward_id: string
  patient_name: string | null
  notes: string | null
  device_id: string | null
  created_at: number
}

interface SlotForm {
  bed_label: string
  expected_ip: string
  ward_id: string
  patient_name: string
  notes: string
}

const EMPTY_SLOT_FORM: SlotForm = {
  bed_label: '', expected_ip: '', ward_id: '', patient_name: '', notes: '',
}

// ── Shared dark-theme input style ─────────────────────────────────
const inputStyle: React.CSSProperties = {
  background: 'var(--vk-bg)',
  border: '1px solid var(--vk-border)',
  color: 'var(--vk-text)',
  borderRadius: '0.5rem',
  padding: '0.375rem 0.625rem',
  fontSize: '0.8125rem',
  width: '100%',
  outline: 'none',
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div>
      <label className="block text-xs font-medium mb-1" style={{ color: 'var(--vk-muted)' }}>{label}</label>
      {children}
    </div>
  )
}

// ─────────────────────────────────────────────────────────────────
export default function Admin() {
  const token    = useAuthStore((s) => s.accessToken)
  const role     = useAuthStore((s) => s.operatorRole)
  const navigate = useNavigate()
  const qc       = useQueryClient()

  const [tab, setTab]             = useState<'devices' | 'commission'>('devices')
  const [editModal, setEditModal] = useState<EditDeviceModal | null>(null)
  const [slotForm, setSlotForm]   = useState<SlotForm>(EMPTY_SLOT_FORM)
  const [editSlot, setEditSlot]   = useState<ProvisionedSlot | null>(null)

  useEffect(() => {
    if (!token || role !== 'admin') navigate('/login')
  }, [token, role, navigate])

  // ── Devices query ──────────────────────────────────────────────
  const { data: devices = [], isLoading: devLoading } = useQuery<Device[]>({
    queryKey: ['admin-devices'],
    queryFn: () => api.get('/admin/devices').then((r) => r.data),
    refetchInterval: 10_000,
  })

  const approveMutation = useMutation({
    mutationFn: (device_id: string) => api.post(`/admin/devices/${device_id}/approve`),
    onSuccess: () => qc.invalidateQueries({ queryKey: ['admin-devices'] }),
  })
  const deleteMutation = useMutation({
    mutationFn: (device_id: string) => api.delete(`/admin/devices/${device_id}`),
    onSuccess: () => qc.invalidateQueries({ queryKey: ['admin-devices'] }),
  })
  const updateMutation = useMutation({
    mutationFn: ({ device_id, patient_name, room_label }: EditDeviceModal) =>
      api.patch(`/admin/devices/${device_id}`, { patient_name, room_label }),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['admin-devices'] }); setEditModal(null) },
  })

  const pending  = devices.filter((d) => !d.approved)
  const approved = devices.filter((d) =>  d.approved)

  // ── Provisioned slots query ─────────────────────────────────────
  const { data: slots = [], isLoading: slotsLoading } = useQuery<ProvisionedSlot[]>({
    queryKey: ['provisioned-slots'],
    queryFn: () => api.get('/admin/provisioned').then((r) => r.data),
    enabled: tab === 'commission',
  })

  const createSlotMutation = useMutation({
    mutationFn: (body: SlotForm) => api.post('/admin/provisioned', {
      bed_label: body.bed_label,
      expected_ip: body.expected_ip,
      ward_id: body.ward_id || '',
      patient_name: body.patient_name || null,
      notes: body.notes || null,
    }),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['provisioned-slots'] }); setSlotForm(EMPTY_SLOT_FORM) },
  })

  const updateSlotMutation = useMutation({
    mutationFn: ({ id, form }: { id: number; form: SlotForm }) =>
      api.put(`/admin/provisioned/${id}`, {
        bed_label: form.bed_label,
        expected_ip: form.expected_ip,
        ward_id: form.ward_id || '',
        patient_name: form.patient_name || null,
        notes: form.notes || null,
      }),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['provisioned-slots'] }); setEditSlot(null) },
  })

  const deleteSlotMutation = useMutation({
    mutationFn: (id: number) => api.delete(`/admin/provisioned/${id}`),
    onSuccess: () => qc.invalidateQueries({ queryKey: ['provisioned-slots'] }),
  })

  // ── Status badge ──────────────────────────────────────────────
  const deviceStatusBadge = (status: string) => {
    const map: Record<string, [string, string]> = {
      ONLINE:    ['#0d4d52', '#67e8f9'],
      EMERGENCY: ['#450a0a', '#fca5a5'],
      OFFLINE:   ['#1e293b', '#94a3b8'],
      PENDING:   ['#422006', '#fcd34d'],
    }
    const [bg, fg] = map[status] ?? map.OFFLINE
    return (
      <span className="text-[10px] font-semibold px-2 py-0.5 rounded-full"
        style={{ background: bg, color: fg }}>{status}</span>
    )
  }

  const cardStyle: React.CSSProperties = {
    background: 'var(--vk-card)',
    border: '1px solid var(--vk-border)',
    borderRadius: '0.75rem',
    padding: '1rem',
  }

  return (
    <div className="min-h-screen" style={{ background: 'var(--vk-bg)' }}>

      {/* Top bar */}
      <header className="sticky top-0 z-50 px-5 py-3 flex items-center justify-between gap-4"
        style={{ background: 'rgba(8,14,26,0.95)', backdropFilter: 'blur(14px)', borderBottom: '1px solid var(--vk-border)' }}>
        <div className="flex items-center gap-3">
          <button onClick={() => navigate('/')}
            className="text-xs font-medium hover:opacity-80 transition"
            style={{ color: 'var(--vk-accent)' }}>
            ← Dashboard
          </button>
          <span className="text-base font-bold" style={{ color: 'var(--vk-text)' }}>
            Painel Admin — VitalKare
          </span>
        </div>
        <button
          onClick={() => { useAuthStore.getState().logout(); navigate('/login') }}
          className="text-xs hover:opacity-80 transition"
          style={{ color: 'var(--vk-muted)' }}>
          Sair
        </button>
      </header>

      {/* Tabs */}
      <div className="max-w-5xl mx-auto px-4 mt-5 flex gap-2">
        {([['devices', '📡 Dispositivos'], ['commission', '🔧 Comissionamento']] as const).map(([key, label]) => (
          <button key={key} onClick={() => setTab(key)}
            className="px-4 py-2 rounded-lg text-sm font-medium transition"
            style={tab === key
              ? { background: 'var(--vk-accent)', color: '#fff' }
              : { background: 'var(--vk-card)', border: '1px solid var(--vk-border)', color: 'var(--vk-muted)' }}>
            {label}
          </button>
        ))}
      </div>

      <main className="max-w-5xl mx-auto px-4 py-5 space-y-6">

        {/* ═══════════════ TAB: DEVICES ═══════════════════════════ */}
        {tab === 'devices' && (
          <>
            {/* Pending */}
            <section>
              <h2 className="text-sm font-semibold uppercase tracking-wider mb-3"
                style={{ color: '#fcd34d' }}>
                Pendentes ({pending.length})
              </h2>
              {devLoading && <p className="text-sm" style={{ color: 'var(--vk-muted)' }}>Carregando...</p>}
              {!devLoading && pending.length === 0 && (
                <p className="text-sm italic" style={{ color: 'var(--vk-muted)' }}>
                  Nenhum dispositivo aguardando aprovação.
                </p>
              )}
              <div className="space-y-3">
                {pending.map((d) => (
                  <div key={d.device_id} style={{ ...cardStyle, borderLeft: '4px solid #f59e0b' }}>
                    <div className="flex items-center justify-between gap-4">
                      <div className="min-w-0">
                        <p className="font-semibold text-sm" style={{ color: 'var(--vk-text)' }}>{d.device_id}</p>
                        <p className="text-xs mt-0.5" style={{ color: 'var(--vk-muted)' }}>
                          Ala: {d.ward_id || '—'} · FW: {d.fw_version || '—'}
                        </p>
                      </div>
                      <div className="flex gap-2 flex-shrink-0">
                        <button onClick={() => approveMutation.mutate(d.device_id)}
                          disabled={approveMutation.isPending}
                          className="rounded-lg px-3 py-1.5 text-xs font-semibold text-white disabled:opacity-50 hover:opacity-80 transition"
                          style={{ background: '#15803d' }}>
                          Aprovar
                        </button>
                        <button
                          onClick={() => { if (confirm(`Remover ${d.device_id}?`)) deleteMutation.mutate(d.device_id) }}
                          className="rounded-lg px-3 py-1.5 text-xs font-semibold hover:opacity-80 transition"
                          style={{ background: '#450a0a', color: '#fca5a5' }}>
                          Recusar
                        </button>
                      </div>
                    </div>
                  </div>
                ))}
              </div>
            </section>

            {/* Approved */}
            <section>
              <h2 className="text-sm font-semibold uppercase tracking-wider mb-3"
                style={{ color: 'var(--vk-muted)' }}>
                Aprovados ({approved.length})
              </h2>
              <div className="space-y-3">
                {approved.map((d) => (
                  <div key={d.device_id} style={cardStyle}>
                    <div className="flex items-center justify-between gap-4">
                      <div className="flex-1 min-w-0">
                        <div className="flex items-center gap-2 flex-wrap">
                          <p className="font-semibold text-sm" style={{ color: 'var(--vk-text)' }}>
                            {d.room_label || d.device_id}
                          </p>
                          {deviceStatusBadge(d.status)}
                        </div>
                        <div className="flex flex-wrap gap-x-4 gap-y-0.5 mt-1 text-xs" style={{ color: 'var(--vk-muted)' }}>
                          <span>ID: {d.device_id}</span>
                          {d.patient_name && <span>👤 {d.patient_name}</span>}
                          <span>Ala: {d.ward_id || '—'}</span>
                          {d.rssi != null && <span>📶 {d.rssi} dBm</span>}
                        </div>
                      </div>
                      <div className="flex gap-2 flex-shrink-0">
                        <button
                          onClick={() => setEditModal({ device_id: d.device_id, patient_name: d.patient_name ?? '', room_label: d.room_label ?? '' })}
                          className="rounded-lg px-3 py-1.5 text-xs font-semibold hover:opacity-80 transition"
                          style={{ background: 'var(--vk-surface)', border: '1px solid var(--vk-border)', color: 'var(--vk-accent)' }}>
                          Editar
                        </button>
                        <button
                          onClick={() => { if (confirm(`Remover ${d.device_id}?`)) deleteMutation.mutate(d.device_id) }}
                          className="rounded-lg px-3 py-1.5 text-xs font-semibold hover:opacity-80 transition"
                          style={{ background: '#450a0a', color: '#fca5a5' }}>
                          Remover
                        </button>
                      </div>
                    </div>
                  </div>
                ))}
              </div>
            </section>

            {/* Server info */}
            <section>
              <h2 className="text-sm font-semibold uppercase tracking-wider mb-3"
                style={{ color: 'var(--vk-muted)' }}>Informações do Servidor</h2>
              <div style={cardStyle} className="text-sm space-y-1.5">
                {[
                  ['🔌', 'TCP VK Protocol', '0.0.0.0:7890'],
                  ['🌐', 'API HTTP',        'http://localhost:8000'],
                  ['📡', 'WebSocket',       'ws://localhost:8000/ws/dashboard'],
                  ['🗄', 'Banco de dados',  'SQLite (vitalkare_mvp.db)'],
                  ['🔑', 'Login admin',     'admin / admin'],
                ].map(([icon, lbl, val]) => (
                  <p key={lbl} style={{ color: 'var(--vk-muted)' }}>
                    {icon} {lbl}: <b style={{ color: 'var(--vk-text)' }}>{val}</b>
                  </p>
                ))}
              </div>
            </section>
          </>
        )}

        {/* ═══════════════ TAB: COMMISSIONING ═════════════════════ */}
        {tab === 'commission' && (
          <>
            <section>
              <div className="flex items-center justify-between mb-4">
                <div>
                  <h2 className="text-base font-semibold" style={{ color: 'var(--vk-text)' }}>
                    Comissionamento de Kiiras
                  </h2>
                  <p className="text-xs mt-0.5" style={{ color: 'var(--vk-muted)' }}>
                    Pré-configure IP esperado por leito. A Kiira com esse IP se aprovará automaticamente ao conectar.
                  </p>
                </div>
              </div>

              {/* Add / Edit form */}
              <div style={{ ...cardStyle, borderLeft: '4px solid var(--vk-accent)' }} className="mb-5">
                <h3 className="text-sm font-semibold mb-3" style={{ color: 'var(--vk-accent)' }}>
                  {editSlot ? `Editar — ${editSlot.bed_label}` : '+ Novo Leito Comissionado'}
                </h3>
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <Field label="Leito / Identificação *">
                    <input style={inputStyle} placeholder="Ex: Leito A, UTI-01"
                      value={editSlot ? editSlot.bed_label : slotForm.bed_label}
                      onChange={(e) => editSlot
                        ? setEditSlot({ ...editSlot, bed_label: e.target.value })
                        : setSlotForm({ ...slotForm, bed_label: e.target.value })} />
                  </Field>
                  <Field label="IP Esperado da Kiira *">
                    <input style={inputStyle} placeholder="Ex: 192.168.1.50"
                      value={editSlot ? editSlot.expected_ip : slotForm.expected_ip}
                      onChange={(e) => editSlot
                        ? setEditSlot({ ...editSlot, expected_ip: e.target.value })
                        : setSlotForm({ ...slotForm, expected_ip: e.target.value })} />
                  </Field>
                  <Field label="Ala / Setor">
                    <input style={inputStyle} placeholder="Ex: UTI, Ala A"
                      value={editSlot ? editSlot.ward_id : slotForm.ward_id}
                      onChange={(e) => editSlot
                        ? setEditSlot({ ...editSlot, ward_id: e.target.value })
                        : setSlotForm({ ...slotForm, ward_id: e.target.value })} />
                  </Field>
                  <Field label="Paciente Previsto">
                    <input style={inputStyle} placeholder="Nome do paciente (opcional)"
                      value={editSlot ? (editSlot.patient_name ?? '') : slotForm.patient_name}
                      onChange={(e) => editSlot
                        ? setEditSlot({ ...editSlot, patient_name: e.target.value })
                        : setSlotForm({ ...slotForm, patient_name: e.target.value })} />
                  </Field>
                  <Field label="Observações">
                    <input style={inputStyle} placeholder="Notas opcionais"
                      value={editSlot ? (editSlot.notes ?? '') : slotForm.notes}
                      onChange={(e) => editSlot
                        ? setEditSlot({ ...editSlot, notes: e.target.value })
                        : setSlotForm({ ...slotForm, notes: e.target.value })} />
                  </Field>
                </div>
                <div className="flex gap-2 mt-4">
                  <button
                    disabled={createSlotMutation.isPending || updateSlotMutation.isPending}
                    onClick={() => {
                      if (editSlot) {
                        updateSlotMutation.mutate({ id: editSlot.id, form: {
                          bed_label: editSlot.bed_label,
                          expected_ip: editSlot.expected_ip,
                          ward_id: editSlot.ward_id,
                          patient_name: editSlot.patient_name ?? '',
                          notes: editSlot.notes ?? '',
                        }})
                      } else {
                        if (!slotForm.bed_label || !slotForm.expected_ip) return
                        createSlotMutation.mutate(slotForm)
                      }
                    }}
                    className="rounded-lg px-4 py-2 text-sm font-semibold text-white disabled:opacity-50 hover:opacity-80 transition"
                    style={{ background: 'var(--vk-accent)' }}>
                    {editSlot
                      ? (updateSlotMutation.isPending ? 'Salvando...' : '💾 Salvar Edição')
                      : (createSlotMutation.isPending ? 'Criando...'  : '+ Adicionar Leito')}
                  </button>
                  {editSlot && (
                    <button onClick={() => setEditSlot(null)}
                      className="rounded-lg px-4 py-2 text-sm hover:opacity-80 transition"
                      style={{ background: 'var(--vk-surface)', border: '1px solid var(--vk-border)', color: 'var(--vk-muted)' }}>
                      Cancelar
                    </button>
                  )}
                </div>
              </div>

              {/* Slots list */}
              {slotsLoading && <p className="text-sm" style={{ color: 'var(--vk-muted)' }}>Carregando...</p>}
              {!slotsLoading && slots.length === 0 && (
                <div className="text-center py-10" style={{ color: 'var(--vk-muted)' }}>
                  <p className="text-3xl mb-2">🔧</p>
                  <p className="text-sm">Nenhum leito comissionado.</p>
                  <p className="text-xs mt-1">Adicione acima para pré-configurar uma Kiira por IP.</p>
                </div>
              )}
              <div className="space-y-3">
                {slots.map((slot) => {
                  const linked = slot.device_id != null
                  return (
                    <div key={slot.id}
                      style={{ ...cardStyle, borderLeft: `4px solid ${linked ? '#00b2bd' : '#f59e0b'}` }}>
                      <div className="flex items-start justify-between gap-4">
                        <div className="min-w-0 flex-1">
                          <div className="flex items-center gap-2 flex-wrap">
                            <span className="font-semibold text-sm" style={{ color: 'var(--vk-text)' }}>
                              {slot.bed_label}
                            </span>
                            <span className="text-[10px] font-semibold px-2 py-0.5 rounded-full"
                              style={linked
                                ? { background: '#0d4d52', color: '#67e8f9' }
                                : { background: '#422006', color: '#fcd34d' }}>
                              {linked ? '✓ Vinculado' : '⏳ Aguardando'}
                            </span>
                          </div>
                          <div className="mt-1.5 grid grid-cols-2 sm:grid-cols-3 gap-x-4 gap-y-0.5 text-xs"
                            style={{ color: 'var(--vk-muted)' }}>
                            <span>🌐 <b style={{ color: 'var(--vk-accent)' }}>{slot.expected_ip}</b></span>
                            {slot.ward_id && <span>🏥 {slot.ward_id}</span>}
                            {slot.patient_name && <span>👤 {slot.patient_name}</span>}
                            {slot.device_id && <span>📟 {slot.device_id}</span>}
                            {slot.notes && <span className="col-span-2">📝 {slot.notes}</span>}
                          </div>
                        </div>
                        <div className="flex gap-2 flex-shrink-0">
                          <button onClick={() => setEditSlot(slot)}
                            className="rounded-lg px-3 py-1.5 text-xs font-semibold hover:opacity-80 transition"
                            style={{ background: 'var(--vk-surface)', border: '1px solid var(--vk-border)', color: 'var(--vk-accent)' }}>
                            Editar
                          </button>
                          <button
                            onClick={() => { if (confirm(`Remover leito "${slot.bed_label}"?`)) deleteSlotMutation.mutate(slot.id) }}
                            className="rounded-lg px-3 py-1.5 text-xs font-semibold hover:opacity-80 transition"
                            style={{ background: '#450a0a', color: '#fca5a5' }}>
                            Remover
                          </button>
                        </div>
                      </div>
                    </div>
                  )
                })}
              </div>

              {/* How it works */}
              <div className="mt-6 rounded-xl p-4 text-xs space-y-1.5"
                style={{ background: 'var(--vk-surface)', border: '1px solid var(--vk-border)', color: 'var(--vk-muted)' }}>
                <p className="font-semibold mb-2" style={{ color: 'var(--vk-text)' }}>Como funciona o comissionamento:</p>
                <p>1. Configure o IP fixo na Kiira (via firmware ou DHCP reservado no roteador).</p>
                <p>2. Cadastre o IP aqui com o rótulo do leito e dados do paciente.</p>
                <p>3. Ao conectar, o servidor detecta o IP e aprova automaticamente — sem intervenção manual.</p>
                <p>4. A Kiira aparece no dashboard já com nome de leito e paciente configurados.</p>
              </div>
            </section>
          </>
        )}

      </main>

      {/* Edit device modal */}
      {editModal && (
        <div className="fixed inset-0 flex items-center justify-center z-50 p-4"
          style={{ background: 'rgba(0,0,0,0.7)', backdropFilter: 'blur(4px)' }}>
          <div className="w-full max-w-sm rounded-2xl shadow-2xl p-6"
            style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)' }}>
            <h3 className="text-base font-bold mb-4" style={{ color: 'var(--vk-text)' }}>
              Editar — <span style={{ color: 'var(--vk-accent)' }}>{editModal.device_id}</span>
            </h3>
            <div className="space-y-3">
              <Field label="Paciente">
                <input style={inputStyle} placeholder="Nome do paciente"
                  value={editModal.patient_name}
                  onChange={(e) => setEditModal({ ...editModal, patient_name: e.target.value })} />
              </Field>
              <Field label="Sala / Leito">
                <input style={inputStyle} placeholder="Ex: Leito 01 — Ala A"
                  value={editModal.room_label}
                  onChange={(e) => setEditModal({ ...editModal, room_label: e.target.value })} />
              </Field>
            </div>
            <div className="flex gap-3 mt-5">
              <button onClick={() => updateMutation.mutate(editModal)} disabled={updateMutation.isPending}
                className="flex-1 rounded-lg py-2 text-sm font-semibold text-white disabled:opacity-50 hover:opacity-80 transition"
                style={{ background: 'var(--vk-accent)' }}>
                {updateMutation.isPending ? 'Salvando...' : 'Salvar'}
              </button>
              <button onClick={() => setEditModal(null)}
                className="flex-1 rounded-lg py-2 text-sm hover:opacity-80 transition"
                style={{ background: 'var(--vk-surface)', border: '1px solid var(--vk-border)', color: 'var(--vk-muted)' }}>
                Cancelar
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

