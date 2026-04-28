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
  BAD_ENVELOPE: 'bad_envelope',
  UNSUPPORTED_VERSION: 'unsupported_version',
  BAD_KIND: 'bad_kind',
  METHOD_NOT_FOUND: 'method_not_found',
  INVALID_PARAMS: 'invalid_params',
  RATE_LIMITED: 'rate_limited',
  INTERNAL_ERROR: 'internal_error',
  NOT_FOUND: 'not_found'
})
