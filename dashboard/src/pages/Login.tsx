import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useAuthStore } from '@/store/authStore'

export default function Login() {
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError]       = useState<string | null>(null)
  const [loading, setLoading]   = useState(false)
  const login    = useAuthStore((s) => s.login)
  const navigate = useNavigate()

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)
    setLoading(true)
    try {
      await login(username, password)
      navigate('/')
    } catch {
      setError('Usuário ou senha inválidos.')
    } finally {
      setLoading(false)
    }
  }

  const inputStyle: React.CSSProperties = {
    background: 'var(--vk-bg)',
    border: '1px solid var(--vk-border)',
    color: 'var(--vk-text)',
    borderRadius: '0.5rem',
    padding: '0.5rem 0.75rem',
    fontSize: '0.875rem',
    width: '100%',
    outline: 'none',
    transition: 'border-color 0.15s',
  }

  return (
    <div className="min-h-screen flex items-center justify-center px-4"
      style={{ background: 'var(--vk-bg)' }}>
      <div className="w-full max-w-sm">

        {/* Logo area */}
        <div className="text-center mb-8">
          <h1 className="text-3xl font-bold tracking-widest uppercase"
            style={{ color: 'var(--vk-accent)' }}>
            VitalKare
          </h1>
          <p className="text-sm mt-1" style={{ color: 'var(--vk-muted)' }}>
            Monitor de Leitos · Acesso da Equipe
          </p>
        </div>

        {/* Card */}
        <div className="rounded-2xl p-7"
          style={{ background: 'var(--vk-card)', border: '1px solid var(--vk-border)', boxShadow: '0 8px 40px rgba(0,0,0,.5)' }}>

          <form onSubmit={handleSubmit} className="space-y-4">
            <div>
              <label className="block text-xs font-medium mb-1.5" style={{ color: 'var(--vk-muted)' }}>
                Usuário
              </label>
              <input
                type="text"
                autoComplete="username"
                required
                value={username}
                onChange={(e) => setUsername(e.target.value)}
                style={inputStyle}
                placeholder="admin"
                onFocus={(e) => (e.target.style.borderColor = 'var(--vk-accent)')}
                onBlur={(e)  => (e.target.style.borderColor = 'var(--vk-border)')}
              />
            </div>
            <div>
              <label className="block text-xs font-medium mb-1.5" style={{ color: 'var(--vk-muted)' }}>
                Senha
              </label>
              <input
                type="password"
                autoComplete="current-password"
                required
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                style={inputStyle}
                onFocus={(e) => (e.target.style.borderColor = 'var(--vk-accent)')}
                onBlur={(e)  => (e.target.style.borderColor = 'var(--vk-border)')}
              />
            </div>

            {error && (
              <p className="text-xs rounded-lg px-3 py-2"
                style={{ background: '#450a0a', color: '#fca5a5', border: '1px solid #7f1d1d' }}>
                {error}
              </p>
            )}

            <button
              type="submit"
              disabled={loading}
              className="w-full rounded-lg py-2.5 text-sm font-semibold text-white disabled:opacity-50 transition hover:opacity-90"
              style={{ background: 'var(--vk-accent)', marginTop: '1.25rem' }}>
              {loading ? 'Entrando...' : 'Entrar'}
            </button>
          </form>
        </div>

        <p className="text-center text-xs mt-5" style={{ color: 'var(--vk-muted)' }}>
          VitalKare MVP · EmbarcaTech Residência
        </p>
      </div>
    </div>
  )
}
