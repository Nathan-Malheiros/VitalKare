import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import api from '@/services/api'

interface AuthState {
  accessToken: string | null
  operatorName: string | null
  operatorRole: string | null
  login: (email: string, password: string) => Promise<void>
  logout: () => void
}

export const useAuthStore = create<AuthState>()(
  persist(
    (set) => ({
      accessToken: null,
      operatorName: null,
      operatorRole: null,

      login: async (email, password) => {
        const form = new FormData()
        form.append('username', email)
        form.append('password', password)
        const { data } = await api.post('/auth/token', form, {
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        })
        // Decodifica claims do JWT sem biblioteca adicional
        const payloadB64 = data.access_token.split('.')[1]
        const claims = JSON.parse(atob(payloadB64.replace(/-/g, '+').replace(/_/g, '/')))
        set({
          accessToken: data.access_token,
          operatorName: claims.name ?? email,
          operatorRole: claims.role ?? null,
        })
      },

      logout: () => {
        set({ accessToken: null, operatorName: null, operatorRole: null })
      },
    }),
    {
      name: 'vitalkare-auth',
      partialize: (state) => ({ accessToken: state.accessToken }),
    }
  )
)
