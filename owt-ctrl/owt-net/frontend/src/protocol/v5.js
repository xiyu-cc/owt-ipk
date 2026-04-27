export const WS_UI_PATH = '/ws/v5/ui'
export const BUS_VERSION = 'v5'

export const BUS_KINDS = Object.freeze({
  ACTION: 'action',
  RESULT: 'result',
  EVENT: 'event',
  ERROR: 'error'
})

export const COMMAND_BUS_ACTIONS = Object.freeze({
  SESSION_SUBSCRIBE: 'session.subscribe',
  AGENT_LIST: 'agent.list',
  PARAMS_GET: 'params.get',
  PARAMS_UPDATE: 'params.update',
  COMMAND_SUBMIT: 'command.submit'
})

export const COMMAND_BUS_EVENTS = Object.freeze({
  AGENT_SNAPSHOT: 'agent.snapshot',
  AGENT_UPDATE: 'agent.update',
  COMMAND_EVENT: 'command.event'
})

export const COMMAND_BUS_ERROR_CODES = Object.freeze({
  RATE_LIMITED: 'rate_limited'
})
