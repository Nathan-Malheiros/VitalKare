import axios, { type AxiosRequestConfig } from 'axios'

const api = axios.create({
  baseURL: '/api/v1',
  headers: { 'Content-Type': 'application/json' },
})

// =========================================================
// REQUEST INTERCEPTOR — injeta token JWT
// =========================================================
api.interceptors.request.use((config) => {
  const token = localStorage.getItem('access_token')
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

// =========================================================
// RESPONSE INTERCEPTOR — trata 401: tenta refresh, depois redireciona
// =========================================================
let isRefreshing = false
let refreshQueue: Array<(token: string) => void> = []

api.interceptors.response.use(
  (res) => res,
  async (error) => {
    const originalRequest = error.config as AxiosRequestConfig & { _retry?: boolean }

    if (error.response?.status === 401 && !originalRequest._retry) {
      const refreshToken = localStorage.getItem('refresh_token')
      if (!refreshToken) {
        _redirectToLogin()
        return Promise.reject(error)
      }

      if (isRefreshing) {
        // Enfileira requisições enquanto o refresh está em andamento
        return new Promise<string>((resolve) => {
          refreshQueue.push(resolve)
        }).then((newToken) => {
          if (originalRequest.headers) {
            originalRequest.headers['Authorization'] = `Bearer ${newToken}`
          }
          return api(originalRequest)
        })
      }

      originalRequest._retry = true
      isRefreshing = true

      try {
        const { data } = await axios.post<{ access_token: string }>('/api/v1/auth/refresh', {
          refresh_token: refreshToken,
        })
        const newToken = data.access_token
        localStorage.setItem('access_token', newToken)

        // Resolve fila com o novo token
        refreshQueue.forEach((cb) => cb(newToken))
        refreshQueue = []

        if (originalRequest.headers) {
          originalRequest.headers['Authorization'] = `Bearer ${newToken}`
        }
        return api(originalRequest)
      } catch {
        _redirectToLogin()
        return Promise.reject(error)
      } finally {
        isRefreshing = false
      }
    }

    return Promise.reject(error)
  }
)

function _redirectToLogin(): void {
  localStorage.removeItem('access_token')
  localStorage.removeItem('refresh_token')
  window.location.href = '/login'
}

export default api

