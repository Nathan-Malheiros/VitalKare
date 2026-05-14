import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const backendHost = env.VITE_BACKEND_HOST || 'localhost'

  return {
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: `http://${backendHost}:8000`,
        changeOrigin: true,
      },
      '/ws': {
        target: `ws://${backendHost}:8000`,
        ws: true,
      },
    },
  },
  build: {
    outDir: 'dist',
    sourcemap: false,
  },
  }
})
