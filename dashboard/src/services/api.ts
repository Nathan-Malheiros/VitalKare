import axios from 'axios'

const api = axios.create({
  baseURL: '/api',
  headers: { 'Content-Type': 'application/json' },
})

const LOGIN_PATH = '/auth/login'

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
// RESPONSE INTERCEPTOR — redireciona para login em 401
// =========================================================
api.interceptors.response.use(
  (res) => res,
  (error) => {
    const requestUrl = String(error.config?.url ?? '')
    if (error.response?.status === 401 && !requestUrl.includes(LOGIN_PATH)) {
      _redirectToLogin()
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
