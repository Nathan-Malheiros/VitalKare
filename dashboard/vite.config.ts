import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const proxyTargetHost = env.VITE_PROXY_TARGET_HOST || '127.0.0.1'

  return {
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, 'src'),
    },
  },
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: `http://${proxyTargetHost}:8000`,
        changeOrigin: true,
      },
      '/ws': {
        target: `ws://${proxyTargetHost}:8000`,
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
