import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig(({ command }) => ({
  plugins: [vue()],
  base: command === 'build' ? '/owt-ctrl/' : '/',
  server: {
    host: '0.0.0.0',
    port: 5173
  }
}))
