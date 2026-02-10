'use strict';
'require rpc';

const callScan = rpc.declare({
	object: 'luci.fancontrol',
	method: 'scan'
});

const callLoadBoardConfig = rpc.declare({
	object: 'luci.fancontrol',
	method: 'loadBoardConfig'
});

const callLoadBoardDefaults = rpc.declare({
	object: 'luci.fancontrol',
	method: 'loadBoardDefaults'
});

const callLoadBoardSchema = rpc.declare({
	object: 'luci.fancontrol',
	method: 'loadBoardSchema'
});

const callApplyBoardConfig = rpc.declare({
	object: 'luci.fancontrol',
	method: 'applyBoardConfig',
	params: [
		'interval', 'control_mode', 'pwm_path', 'pwm_enable_path', 'control_mode_path',
		'pwm_min', 'pwm_max', 'ramp_up', 'ramp_down',
		'hysteresis_mC', 'failsafe_pwm', 'sources'
	]
});

const callServiceAction = rpc.declare({
	object: 'luci.fancontrol',
	method: 'serviceAction',
	params: [ 'action' ]
});


const callRuntimeStatus = rpc.declare({
	object: 'luci.fancontrol',
	method: 'runtimeStatus'
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
		boardSchema: {},
		runtimeState: {},
		hasQmodem: false,
		failures: []
	};

	const fields = [ 'scan', 'loadedBoard', 'boardSchema', 'runtimeState', 'hasQmodem' ];
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

function loadInitial() {
	return Promise.allSettled([
		callScan(),
		callLoadBoardConfig(),
		callLoadBoardSchema(),
		callRuntimeStatus(),
		probeQmodem()
	]).then(mapSettledLoad);
}

function applyBoardConfig(payload) {
	const p = payload || {};
	return callApplyBoardConfig(
		p.interval,
		p.control_mode,
		p.pwm_path,
		p.pwm_enable_path,
		p.control_mode_path,
		p.pwm_min,
		p.pwm_max,
		p.ramp_up,
		p.ramp_down,
		p.hysteresis_mC,
		p.failsafe_pwm,
		p.sources
	);
}

return L.Class.extend({
	mapSettledLoad: mapSettledLoad,
	loadInitial: loadInitial,
	loadBoardConfig: callLoadBoardConfig,
	loadBoardDefaults: callLoadBoardDefaults,
	loadBoardSchema: callLoadBoardSchema,
	runtimeStatus: callRuntimeStatus,
	serviceAction: callServiceAction,
	applyBoardConfig: applyBoardConfig
});
