import { ref } from 'vue'
import { RPC_ERROR_CODES, WS_UI_PATH } from '../protocol/v4'

function buildWsUrl(path) {
  const base = new URL(window.location.href)
  const protocol = base.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${protocol}//${base.host}${path}`
}

export function useRpcSocket(path = WS_UI_PATH) {
  const connected = ref(false)
  const lastError = ref('')

  let ws = null
  let reconnectTimer = null
  let reqSeq = 0
  const pending = new Map()
  const handlers = new Map()

  function emit(method, params) {
    const set = handlers.get(method)
    if (!set) return
    for (const cb of set) {
      try {
        cb(params)
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

      if (Object.prototype.hasOwnProperty.call(msg, 'id')) {
        const req = pending.get(String(msg.id))
        if (!req) return
        pending.delete(String(msg.id))
        if (msg.error) {
          const message = msg?.error?.message || 'rpc error'
          const error = new Error(message)
          error.rpc = msg.error
          error.code = msg?.error?.data?.code || ''
          error.retryAfterMs = msg?.error?.data?.retry_after_ms || 0
          if (error.code === RPC_ERROR_CODES.RATE_LIMITED && error.retryAfterMs > 0) {
            error.message = `${message} (retry after ${error.retryAfterMs}ms)`
          }
          req.reject(error)
          return
        }
        req.resolve(msg.result || {})
        return
      }

      if (typeof msg?.method === 'string') {
        emit(msg.method, msg.params || {})
      }
    }

    ws.onclose = () => {
      connected.value = false
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
  }

  function call(method, params = {}) {
    return new Promise((resolve, reject) => {
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        reject(new Error('ws not connected'))
        return
      }
      const id = String(++reqSeq)
      pending.set(id, { resolve, reject })
      ws.send(
        JSON.stringify({
          jsonrpc: '2.0',
          id,
          method,
          params
        })
      )
      setTimeout(() => {
        const req = pending.get(id)
        if (!req) return
        pending.delete(id)
        reject(new Error('rpc timeout'))
      }, 15000)
    })
  }

  function notify(method, params = {}) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      return false
    }
    ws.send(
      JSON.stringify({
        jsonrpc: '2.0',
        method,
        params
      })
    )
    return true
  }

  function on(method, cb) {
    if (!handlers.has(method)) {
      handlers.set(method, new Set())
    }
    handlers.get(method).add(cb)
    return () => {
      const set = handlers.get(method)
      if (!set) return
      set.delete(cb)
      if (set.size === 0) {
        handlers.delete(method)
      }
    }
  }

  return {
    connected,
    lastError,
    connect,
    close,
    call,
    notify,
    on
  }
}
