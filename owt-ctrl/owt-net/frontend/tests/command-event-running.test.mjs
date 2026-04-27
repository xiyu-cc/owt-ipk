import assert from 'node:assert/strict'

import { useCommandBusSocket } from '../src/composables/useCommandBusSocket.js'
import { BUS_KINDS, BUS_VERSION, COMMAND_BUS_EVENTS, WS_UI_PATH } from '../src/protocol/v5.js'

class FakeWebSocket {
  static CONNECTING = 0
  static OPEN = 1
  static CLOSING = 2
  static CLOSED = 3
  static instances = []

  constructor(url) {
    this.url = url
    this.readyState = FakeWebSocket.CONNECTING
    this.sent = []
    this.onopen = null
    this.onmessage = null
    this.onclose = null
    this.onerror = null
    FakeWebSocket.instances.push(this)
  }

  send(data) {
    this.sent.push(data)
  }

  close() {
    this.readyState = FakeWebSocket.CLOSED
    if (typeof this.onclose === 'function') {
      this.onclose()
    }
  }

  emitOpen() {
    this.readyState = FakeWebSocket.OPEN
    if (typeof this.onopen === 'function') {
      this.onopen()
    }
  }

  emitMessage(data) {
    if (typeof this.onmessage === 'function') {
      this.onmessage({ data })
    }
  }
}

globalThis.window = {
  location: { href: 'http://127.0.0.1:3000/dashboard' }
}
globalThis.WebSocket = FakeWebSocket

const bus = useCommandBusSocket(WS_UI_PATH)
const received = []
const off = bus.on(COMMAND_BUS_EVENTS.COMMAND_EVENT, (payload) => {
  received.push(payload)
})

bus.connect()
assert.equal(FakeWebSocket.instances.length, 1, 'socket should be created')

const ws = FakeWebSocket.instances[0]
assert.equal(ws.url, 'ws://127.0.0.1:3000/ws/v5/ui', 'ui ws path should stay unchanged')
ws.emitOpen()

ws.emitMessage(
  JSON.stringify({
    v: BUS_VERSION,
    kind: BUS_KINDS.EVENT,
    name: COMMAND_BUS_EVENTS.COMMAND_EVENT,
    id: 'evt-1',
    ts_ms: Date.now(),
    payload: {
      resource: {
        command: {
          command_id: 'cmd-1',
          command_type: 'host_reboot',
          agent_mac: 'AA:00:00:00:90:01'
        },
        event: {
          event_type: 'command_ack_received',
          status: 'running'
        }
      }
    }
  })
)

assert.equal(received.length, 1, 'command.event should be dispatched to subscribers')
assert.equal(
  received[0]?.resource?.event?.status,
  'running',
  'running status should be preserved in event payload'
)

off()
bus.close()
console.log('command-event-running.test passed')
