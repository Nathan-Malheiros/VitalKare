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

      login: async (username, password) => {
        const { data } = await api.post('/auth/login', { username, password })
        set({
          accessToken: data.access_token,
          operatorName: data.username ?? username,
          operatorRole: data.role ?? null,
        })
        localStorage.setItem('access_token', data.access_token)
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
