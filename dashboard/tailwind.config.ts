/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        brand: {
          50:  '#eff6ff',
          100: '#dbeafe',
          500: '#3b82f6',
          600: '#2563eb',
          700: '#1d4ed8',
          900: '#1e3a8a',
        },
        // VitalKare dark theme
        vk: {
          bg:      '#080e1a',
          surface: '#0a1628',
          card:    '#0d1b2e',
          border:  '#1e2d40',
          text:    '#e2e8f0',
          muted:   '#94a3b8',
          accent:  '#00b2bd',
        },
        emergency: '#ef4444',
        warning:   '#f59e0b',
        success:   '#22c55e',
        offline:   '#6b7280',
      },
      animation: {
        'pulse-glow': 'pulse-glow 1.6s ease-in-out infinite',
        'banner-drop': 'banner-drop 0.3s ease-out forwards',
      },
      keyframes: {
        'pulse-glow': {
          '0%, 100%': { boxShadow: '0 0 0 0 rgba(239,68,68,0.4)' },
          '50%':      { boxShadow: '0 0 0 10px rgba(239,68,68,0)' },
        },
        'banner-drop': {
          from: { transform: 'translateY(-100%)', opacity: '0' },
          to:   { transform: 'none', opacity: '1' },
        },
      },
    },
  },
  plugins: [],
}
