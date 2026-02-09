'use strict';
'require rpc';
'require fancontrol.validators as validators';

const callScan = rpc.declare({
	object: 'luci.fancontrol',
	method: 'scan'
});

const callLoadBoardConfig = rpc.declare({
	object: 'luci.fancontrol',
	method: 'loadBoardConfig',
	params: [ 'path' ]
});

const callApplyBoardConfig = rpc.declare({
	object: 'luci.fancontrol',
	method: 'applyBoardConfig',
	params: [
		'output', 'interval', 'pwm_path', 'pwm_enable_path', 'thermal_mode_path',
		'pwm_min', 'pwm_max', 'pwm_inverted', 'pwm_startup_pwm', 'ramp_up', 'ramp_down',
		'hysteresis_mC', 'policy', 'failsafe_pwm', 'entries'
	]
});

const callGetControlMode = rpc.declare({
	object: 'luci.fancontrol',
	method: 'getControlMode',
	params: [ 'path' ]
});

const callServiceAction = rpc.declare({
	object: 'luci.fancontrol',
	method: 'serviceAction',
	params: [ 'action' ]
});

const callSetControlMode = rpc.declare({
	object: 'luci.fancontrol',
	method: 'setControlMode',
	params: [ 'mode', 'path' ]
});

const callRuntimeStatus = rpc.declare({
	object: 'luci.fancontrol',
	method: 'runtimeStatus',
	params: [ 'path' ]
});

function probeQmodem() {
	return rpc.list('qmodem').then((signatures) => {
		return !!(signatures && signatures.qmodem && signatures.qmodem.get_temperature);
	}).catch(() => false);
}

function mapSettledLoad(results) {
	const data = {
		scan: {},
		loadedBoard: {},
		controlState: {},
		runtimeState: {},
		hasQmodem: false,
		failures: []
	};

	const fields = [ 'scan', 'loadedBoard', 'controlState', 'runtimeState', 'hasQmodem' ];
	for (let i = 0; i < fields.length; i++) {
		const key = fields[i];
		const item = results[i];
		if (item && item.status === 'fulfilled')
			data[key] = item.value;
		else
			data.failures.push(key);
	}

	if (data.hasQmodem !== true)
		data.hasQmodem = false;

	return data;
}

function loadInitial(path) {
	const requestPath = validators.safeField(path || validators.DEFAULTS.CONFIG_PATH) || validators.DEFAULTS.CONFIG_PATH;
	return Promise.allSettled([
		callScan(),
		callLoadBoardConfig(requestPath),
		callGetControlMode(requestPath),
		callRuntimeStatus(requestPath),
		probeQmodem()
	]).then(mapSettledLoad);
}

function applyBoardConfig(payload) {
	const p = payload || {};
	return callApplyBoardConfig(
		p.output,
		p.interval,
		p.pwm_path,
		p.pwm_enable_path,
		p.thermal_mode_path,
		p.pwm_min,
		p.pwm_max,
		p.pwm_inverted,
		p.pwm_startup_pwm,
		p.ramp_up,
		p.ramp_down,
		p.hysteresis_mC,
		p.policy,
		p.failsafe_pwm,
		p.entries
	);
}

return L.Class.extend({
	mapSettledLoad: mapSettledLoad,
	loadInitial: loadInitial,
	loadBoardConfig: callLoadBoardConfig,
	getControlMode: callGetControlMode,
	setControlMode: callSetControlMode,
	runtimeStatus: callRuntimeStatus,
	serviceAction: callServiceAction,
	applyBoardConfig: applyBoardConfig
});
