import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import axios from 'axios'
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
        try {
          const { data } = await api.post('/auth/login', { username, password })
          set({
            accessToken: data.access_token,
            operatorName: data.username ?? username,
            operatorRole: data.role ?? null,
          })
          localStorage.setItem('access_token', data.access_token)
        } catch (error) {
          if (axios.isAxiosError(error)) {
            if (error.response?.status === 401) {
              throw new Error('Usuario ou senha invalidos.')
            }

            if (error.response == null && error.request != null) {
              throw new Error('Nao foi possivel conectar ao servidor. Verifique se o backend esta rodando na porta 8000.')
            }

            const detail = error.response?.data?.detail
            if (typeof detail === 'string' && detail.trim()) {
              throw new Error(detail)
            }
          }

          throw new Error('Nao foi possivel concluir o login agora.')
        }
      },

      logout: () => {
        localStorage.removeItem('access_token')
        set({ accessToken: null, operatorName: null, operatorRole: null })
      },
    }),
    {
      name: 'vitalkare-auth',
      partialize: (state) => ({ accessToken: state.accessToken }),
    }
  )
)
