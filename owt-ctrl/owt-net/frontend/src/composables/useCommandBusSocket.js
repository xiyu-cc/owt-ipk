import { ref } from 'vue'
import {
  BUS_KINDS,
  BUS_VERSION,
  COMMAND_BUS_ERROR_CODES,
  WS_UI_PATH
} from '../protocol/v5.js'

function buildWsUrl(path) {
  const base = new URL(window.location.href)
  const protocol = base.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${protocol}//${base.host}${path}`
}

function nowMs() {
  return Date.now()
}

export function useCommandBusSocket(path = WS_UI_PATH) {
  const connected = ref(false)
  const lastError = ref('')

  let ws = null
  let reconnectTimer = null
  let reqSeq = 0
  const pending = new Map()
  const handlers = new Map()

  function emit(name, payload) {
    const set = handlers.get(name)
    if (!set) return
    for (const cb of set) {
      try {
        cb(payload)
      } catch (_) {
        // ignore subscriber failure
      }
    }
  }

  function clearReconnect() {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  function scheduleReconnect() {
    if (reconnectTimer) return
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null
      connect()
    }, 1500)
  }

  function clearPending(reason) {
    for (const [id, req] of pending.entries()) {
      pending.delete(id)
      req.reject(new Error(reason))
    }
  }

  function connect() {
    clearReconnect()
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
      return
    }

    try {
      ws = new WebSocket(buildWsUrl(path))
    } catch (err) {
      connected.value = false
      lastError.value = err instanceof Error ? err.message : String(err)
      scheduleReconnect()
      return
    }

    ws.onopen = () => {
      connected.value = true
      lastError.value = ''
      emit('__open__', {})
    }

    ws.onmessage = (event) => {
      let msg = null
      try {
        msg = JSON.parse(event.data)
      } catch (_) {
        return
      }
      if (!msg || typeof msg !== 'object') {
        return
      }
      if (msg.v !== BUS_VERSION || typeof msg.kind !== 'string' || typeof msg.name !== 'string') {
        return
      }

      if (msg.kind === BUS_KINDS.RESULT || msg.kind === BUS_KINDS.ERROR) {
        const hasId =
          msg.id === null || ['string', 'number'].includes(typeof msg.id)
        if (!hasId) {
          return
        }
        const req = pending.get(String(msg.id))
        if (!req) {
          return
        }
        pending.delete(String(msg.id))

        if (msg.kind === BUS_KINDS.ERROR) {
          const payload = msg.payload && typeof msg.payload === 'object' ? msg.payload : {}
          const message = typeof payload.message === 'string' ? payload.message : 'bus error'
          const error = new Error(message)
          error.bus = payload
          error.code = typeof payload.code === 'string' ? payload.code : ''
          error.retryAfterMs =
            payload.detail && typeof payload.detail.retry_after_ms === 'number'
              ? payload.detail.retry_after_ms
              : 0
          if (error.code === COMMAND_BUS_ERROR_CODES.RATE_LIMITED && error.retryAfterMs > 0) {
            error.message = `${message} (retry after ${error.retryAfterMs}ms)`
          }
          req.reject(error)
          return
        }

        req.resolve(msg.payload && typeof msg.payload === 'object' ? msg.payload : {})
        return
      }

      if (msg.kind === BUS_KINDS.EVENT) {
        emit(msg.name, msg.payload && typeof msg.payload === 'object' ? msg.payload : {})
      }
    }

    ws.onclose = () => {
      connected.value = false
      clearPending('socket closed')
      scheduleReconnect()
    }

    ws.onerror = () => {
      // onclose handles reconnect
    }
  }

  function close() {
    clearReconnect()
    if (ws) {
      ws.onopen = null
      ws.onmessage = null
      ws.onclose = null
      ws.onerror = null
      ws.close()
      ws = null
    }
    connected.value = false
    clearPending('socket closed')
  }

  function call(name, payload = {}) {
    return new Promise((resolve, reject) => {
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        reject(new Error('ws not connected'))
        return
      }
      const id = String(++reqSeq)
      pending.set(id, { resolve, reject })
      ws.send(
        JSON.stringify({
          v: BUS_VERSION,
          kind: BUS_KINDS.ACTION,
          name,
          id,
          ts_ms: nowMs(),
          payload: payload && typeof payload === 'object' ? payload : {}
        })
      )
      setTimeout(() => {
        const req = pending.get(id)
        if (!req) return
        pending.delete(id)
        reject(new Error('bus timeout'))
      }, 15000)
    })
  }

  function on(name, cb) {
    if (!handlers.has(name)) {
      handlers.set(name, new Set())
    }
    handlers.get(name).add(cb)
    return () => {
      const set = handlers.get(name)
      if (!set) return
      set.delete(cb)
      if (set.size === 0) {
        handlers.delete(name)
      }
    }
  }

  return {
    connected,
    lastError,
    connect,
    close,
    call,
    on
  }
}
