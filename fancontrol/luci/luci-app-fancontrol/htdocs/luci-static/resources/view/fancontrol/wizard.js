'use strict';
'require rpc';
'require ui';
'require view';

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
		'hysteresis_mC', 'policy', 'failsafe_pwm', 'pidfile', 'entries'
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

function intInRange(value, fallback, minv, maxv) {
	let n = parseInt(value, 10);

	if (isNaN(n))
		n = fallback;
	if (n < minv)
		n = minv;
	if (n > maxv)
		n = maxv;

	return n;
}

function safeField(value) {
	return String(value != null ? value : '').replace(/[;\r\n]/g, '').trim();
}

function relToAbsPath(path) {
	const p = safeField(path);

	if (!p)
		return '';
	if (p.charAt(0) === '/')
		return p;
	if (p.indexOf('hwmon') === 0)
		return '/sys/class/hwmon/' + p;

	return p;
}

function defaultSources(channels) {
	const out = [];
	let nvmePath = '';

	for (let i = 0; i < channels.length; i++) {
		const c = channels[i] || {};
		const dev = String(c.devname || '').toLowerCase();
		if (!nvmePath && dev.indexOf('nvme') >= 0)
			nvmePath = relToAbsPath(c.temp_rel || '');
	}

	if (!nvmePath && channels.length > 0)
		nvmePath = relToAbsPath(channels[0].temp_rel || '');
	if (!nvmePath)
		nvmePath = '/sys/class/hwmon/hwmon0/temp1_input';

	out.push({
		enabled: true,
		id: 'soc',
		type: 'sysfs',
		path: '/sys/class/thermal/thermal_zone0/temp',
		object: '',
		method: '',
		key: '',
		args: '{}',
		t_start: 60000,
		t_full: 82000,
		t_crit: 90000,
		ttl: 6,
		poll: 2,
		weight: 100
	});

	out.push({
		enabled: true,
		id: 'nvme',
		type: 'sysfs',
		path: nvmePath,
		object: '',
		method: '',
		key: '',
		args: '{}',
		t_start: 50000,
		t_full: 70000,
		t_crit: 80000,
		ttl: 6,
		poll: 2,
		weight: 120
	});

	out.push({
		enabled: true,
		id: 'rm500',
		type: 'ubus',
		path: '',
		object: 'qmodem',
		method: 'get_temperature',
		key: 'temp_mC',
		args: '{"config_section":"modem1"}',
		t_start: 58000,
		t_full: 76000,
		t_crit: 85000,
		ttl: 20,
		poll: 10,
		weight: 130
	});

	return out;
}

return view.extend({
	load: function() {
		return Promise.all([
			callScan(),
			callLoadBoardConfig('/etc/fancontrol.r3mini'),
			callGetControlMode('/etc/fancontrol.r3mini')
		]);
	},

	refreshTypeFields: function(row) {
		const isSysfs = row.type.value === 'sysfs';
		const isUbus = row.type.value === 'ubus';

		row.path.disabled = !isSysfs;
		row.object.disabled = !isUbus;
		row.method.disabled = !isUbus;
		row.key.disabled = !isUbus;
		row.args.disabled = !isUbus;
	},

	createSourceRow: function(initial, tbody) {
		const enabled = E('input', { 'type': 'checkbox', 'checked': initial.enabled !== false });
		const id = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'value': safeField(initial.id || ''),
			'style': 'min-width:7em'
		});
		const type = E('select', { 'class': 'cbi-input-select', 'style': 'min-width:6em' }, [
			E('option', { 'value': 'sysfs', 'selected': initial.type === 'sysfs' || !initial.type }, _('sysfs')),
			E('option', { 'value': 'ubus', 'selected': initial.type === 'ubus' }, _('ubus'))
		]);
		const path = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'value': safeField(initial.path || ''),
			'placeholder': '/sys/class/.../temp*_input',
			'style': 'min-width:16em'
		});
		const object = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'value': safeField(initial.object || ''),
			'placeholder': 'qmodem',
			'style': 'min-width:8em'
		});
		const method = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'value': safeField(initial.method || ''),
			'placeholder': 'get_temperature',
			'style': 'min-width:9em'
		});
		const key = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'value': safeField(initial.key || ''),
			'placeholder': 'temp_mC',
			'style': 'min-width:7em'
		});
		const args = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'value': safeField(initial.args || '{}'),
			'placeholder': '{}',
			'style': 'min-width:8em'
		});

		const tStart = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': String(intInRange(initial.t_start, 60000, 10000, 200000)), 'style': 'width:7em' });
		const tFull = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': String(intInRange(initial.t_full, 80000, 10000, 220000)), 'style': 'width:7em' });
		const tCrit = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': String(intInRange(initial.t_crit, 90000, 10000, 250000)), 'style': 'width:7em' });
		const ttl = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': String(intInRange(initial.ttl, 10, 1, 3600)), 'style': 'width:5em' });
		const poll = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': String(intInRange(initial.poll, 2, 1, 3600)), 'style': 'width:5em' });
		const weight = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': String(intInRange(initial.weight, 100, 1, 200)), 'style': 'width:5em' });

		const removeBtn = E('button', {
			'class': 'btn cbi-button cbi-button-remove',
			'type': 'button',
			'click': ui.createHandlerFn(this, function() {
				tbody.removeChild(tr);
				this._sourceRows = this._sourceRows.filter((r) => r !== row);
			})
		}, _('Delete'));

		const tr = E('tr', [
			E('td', enabled),
			E('td', id),
			E('td', type),
			E('td', path),
			E('td', object),
			E('td', method),
			E('td', key),
			E('td', args),
			E('td', tStart),
			E('td', tFull),
			E('td', tCrit),
			E('td', ttl),
			E('td', poll),
			E('td', weight),
			E('td', removeBtn)
		]);

		const row = {
			enabled,
			id,
			type,
			path,
			object,
			method,
			key,
			args,
			tStart,
			tFull,
			tCrit,
			ttl,
			poll,
			weight,
			tr
		};

		type.addEventListener('change', ui.createHandlerFn(this, function() {
			this.refreshTypeFields(row);
		}));

		this.refreshTypeFields(row);
		this._sourceRows.push(row);
		tbody.appendChild(tr);
	},

	fillRows: function(sourceList, tbody) {
		while (tbody.firstChild)
			tbody.removeChild(tbody.firstChild);
		this._sourceRows = [];

		for (let i = 0; i < sourceList.length; i++)
			this.createSourceRow(sourceList[i], tbody);
	},

	applyLoadedBoardConfig: function(cfg, refs, notifyResult) {
		if (!cfg || !cfg.ok)
			return;

		if (!cfg.exists) {
			if (notifyResult)
				ui.addNotification(null, E('p', _('No readable board config file at %s').format(cfg.path || '/etc/fancontrol.r3mini')), 'info');
			return;
		}

		refs.intervalInput.value = String(intInRange(cfg.interval, 2, 1, 3600));
		refs.pwmPathInput.value = safeField(cfg.pwm_path || refs.pwmPathInput.value);
		refs.pwmEnableInput.value = safeField(cfg.pwm_enable_path || refs.pwmEnableInput.value);
		refs.thermalModePathInput.value = safeField(cfg.thermal_mode_path || refs.thermalModePathInput.value);
		refs.pwmMinInput.value = String(intInRange(cfg.pwm_min, 0, 0, 255));
		refs.pwmMaxInput.value = String(intInRange(cfg.pwm_max, 255, 0, 255));
		refs.pwmInvertedInput.checked = !!cfg.pwm_inverted;
		refs.pwmStartupInput.value = String(intInRange(cfg.pwm_startup_pwm, 128, -1, 255));
		refs.rampUpInput.value = String(intInRange(cfg.ramp_up, 25, 1, 255));
		refs.rampDownInput.value = String(intInRange(cfg.ramp_down, 8, 1, 255));
		refs.hystInput.value = String(intInRange(cfg.hysteresis_mC, 2000, 0, 100000));
		refs.policyInput.value = safeField(cfg.policy || 'max') || 'max';
		refs.failsafeInput.value = String(intInRange(cfg.failsafe_pwm, 64, 0, 255));
		refs.pidfileInput.value = safeField(cfg.pidfile || '/var/run/fancontrol.pid');

		const src = Array.isArray(cfg.sources) ? cfg.sources : [];
		if (src.length)
			this.fillRows(src, refs.tbody);

		if (notifyResult)
			ui.addNotification(null, E('p', _('Loaded %d source(s) from %s').format(src.length, cfg.path || '/etc/fancontrol.r3mini')), 'info');
	},

	formatModeStatus: function(info) {
		const mode = safeField(info && info.mode ? info.mode : 'kernel').toLowerCase();
		const modeLabel = mode === 'fancontrol' ? 'fancontrol' : 'kernel';
		const running = info && info.running ? 'yes' : 'no';
		const enabled = info && info.enabled ? 'yes' : 'no';
		const thermalModePath = safeField(info && info.thermal_mode_path ? info.thermal_mode_path : '-');
		const thermalMode = safeField(info && info.thermal_mode ? info.thermal_mode : '-');

		return _('Mode: %s | service running: %s | service enabled: %s | thermal mode: %s (%s)')
			.format(modeLabel, running, enabled, thermalMode, thermalModePath);
	},

	applyControlModeInfo: function(info, refs) {
		if (!refs)
			return;

		const mode = safeField(info && info.mode ? info.mode : 'kernel').toLowerCase();
		if (refs.controlModeInput)
			refs.controlModeInput.value = (mode === 'fancontrol') ? 'fancontrol' : 'kernel';
		if (refs.modeStatus)
			refs.modeStatus.textContent = this.formatModeStatus(info || {});
	},

	refreshControlMode: async function(refs, notifyResult) {
		const path = (refs.outputInput.value || '/etc/fancontrol.r3mini').trim() || '/etc/fancontrol.r3mini';
		const state = await callGetControlMode(path);

		if (!state || !state.ok) {
			if (notifyResult)
				ui.addNotification(null, E('p', _('Failed to query control mode.')), 'danger');
			return;
		}

		this.applyControlModeInfo(state, refs);
		if (notifyResult)
			ui.addNotification(null, E('p', _('Current control mode: %s').format(safeField(state.mode || 'kernel'))), 'info');
	},

	handleSwitchControlMode: async function(refs) {
		const mode = safeField(refs.controlModeInput.value || 'kernel').toLowerCase();
		const path = (refs.outputInput.value || '/etc/fancontrol.r3mini').trim() || '/etc/fancontrol.r3mini';
		const result = await callSetControlMode(mode, path);

		if (!result || !result.ok) {
			ui.addNotification(null, E('p', _('Failed to switch control mode: %s').format(result ? (result.error || 'unknown error') : 'rpc failed')), 'danger');
			return;
		}

		this.applyControlModeInfo(result, refs);
		ui.addNotification(null, E('p', _('Control mode switched to %s').format(safeField(result.mode || mode))), 'info');
	},

	handleLoadExisting: async function(refs) {
		const path = (refs.outputInput.value || '/etc/fancontrol.r3mini').trim() || '/etc/fancontrol.r3mini';
		const cfg = await callLoadBoardConfig(path);

		if (!cfg || !cfg.ok) {
			ui.addNotification(null, E('p', _('Failed to load board config: %s').format(cfg ? (cfg.error || 'unknown error') : 'rpc failed')), 'danger');
			return;
		}

		this.applyLoadedBoardConfig(cfg, refs, true);
		await this.refreshControlMode(refs, false);
	},

	collectEntries: function() {
		const lines = [];
		let active = 0;

		for (let i = 0; i < this._sourceRows.length; i++) {
			const row = this._sourceRows[i];
			const enabled = row.enabled.checked ? '1' : '0';
			const sid = safeField(row.id.value) || ('source' + (i + 1));
			const type = safeField(row.type.value || 'sysfs').toLowerCase();
			const sourcePath = safeField(row.path.value);
			const object = safeField(row.object.value);
			const method = safeField(row.method.value);
			const key = safeField(row.key.value);
			const args = safeField(row.args.value || '{}') || '{}';

			const tStart = intInRange(row.tStart.value, 60000, 10000, 200000);
			let tFull = intInRange(row.tFull.value, 80000, 10000, 220000);
			let tCrit = intInRange(row.tCrit.value, 90000, 10000, 250000);
			let ttl = intInRange(row.ttl.value, 10, 1, 3600);
			const poll = intInRange(row.poll.value, 2, 1, 3600);
			const weight = intInRange(row.weight.value, 100, 1, 200);

			if (enabled === '1')
				active++;
			if (tStart >= tFull)
				tFull = tStart + 10000;
			if (tFull > tCrit)
				tCrit = tFull;
			if (ttl < poll)
				ttl = poll;

			lines.push([
				enabled,
				sid,
				type,
				sourcePath,
				object,
				method,
				key,
				args,
				String(tStart),
				String(tFull),
				String(tCrit),
				String(ttl),
				String(poll),
				String(weight)
			].join(';'));
		}

		return {
			lines: lines,
			active: active
		};
	},

	handleGenerate: async function(refs) {
		const interval = intInRange(refs.intervalInput.value, 2, 1, 3600);
		const output = (refs.outputInput.value || '/etc/fancontrol.r3mini').trim() || '/etc/fancontrol.r3mini';
		const pwmPath = safeField(refs.pwmPathInput.value);
		const pwmEnablePath = safeField(refs.pwmEnableInput.value);
		const thermalModePath = safeField(refs.thermalModePathInput.value || '/sys/class/thermal/thermal_zone0/mode');
		let pwmMin = intInRange(refs.pwmMinInput.value, 0, 0, 255);
		const pwmMax = intInRange(refs.pwmMaxInput.value, 255, 0, 255);
		const pwmInverted = refs.pwmInvertedInput.checked ? 1 : 0;
		const pwmStartup = intInRange(refs.pwmStartupInput.value, 128, -1, 255);
		const rampUp = intInRange(refs.rampUpInput.value, 25, 1, 255);
		const rampDown = intInRange(refs.rampDownInput.value, 8, 1, 255);
		const hysteresis = intInRange(refs.hystInput.value, 2000, 0, 100000);
		const policy = safeField(refs.policyInput.value || 'max').toLowerCase() || 'max';
		const failsafe = intInRange(refs.failsafeInput.value, 64, 0, 255);
		const pidfile = safeField(refs.pidfileInput.value || '/var/run/fancontrol.pid') || '/var/run/fancontrol.pid';

		if (pwmMin > pwmMax)
			pwmMin = pwmMax;

		if (!pwmPath) {
			ui.addNotification(null, E('p', _('PWM path is required.')), 'danger');
			return;
		}

		const pack = this.collectEntries();
		if (pack.active < 1) {
			ui.addNotification(null, E('p', _('Please enable at least one source.')), 'danger');
			return;
		}

		const result = await callApplyBoardConfig(
			output,
			String(interval),
			pwmPath,
			pwmEnablePath,
			thermalModePath,
			String(pwmMin),
			String(pwmMax),
			String(pwmInverted),
			String(pwmStartup),
			String(rampUp),
			String(rampDown),
			String(hysteresis),
			policy,
			String(failsafe),
			pidfile,
			pack.lines.join('\n')
		);

		if (!result || !result.ok) {
			ui.addNotification(null, E('p', _('Failed to generate board config: %s').format(result ? (result.error || 'unknown error') : 'rpc failed')), 'danger');
			return;
		}

		if (refs.restartInput.checked) {
			const svc = await callServiceAction('restart');
			if (!svc || !svc.ok) {
				ui.addNotification(null, E('p', _('Config saved, but restarting fancontrol failed.')), 'danger');
				return;
			}
		}

		await this.refreshControlMode(refs, false);
		ui.addNotification(null, E('p', _('Board configuration written to %s').format(result.output || output)), 'info');
	},

	render: function(data) {
		const scan = data[0] || {};
		const loadedBoard = data[1] || {};
		const controlState = data[2] || {};
		const channels = Array.isArray(scan.channels) ? scan.channels : [];
		this._sourceRows = [];

		const intervalInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '2', 'min': '1', 'max': '3600', 'style': 'width:8em' });
		const outputInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': '/etc/fancontrol.r3mini', 'style': 'min-width:24em' });
		const pwmPathInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': channels.length ? ('/sys/class/hwmon/' + (channels[0].pwm_rel || 'hwmon0/pwm1')) : '/sys/class/hwmon/hwmon0/pwm1', 'style': 'min-width:24em' });
		const pwmEnableInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': '', 'style': 'min-width:24em' });
		const thermalModePathInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': '/sys/class/thermal/thermal_zone0/mode', 'style': 'min-width:24em' });
		const pwmMinInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '0', 'min': '0', 'max': '255', 'style': 'width:8em' });
		const pwmMaxInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '255', 'min': '0', 'max': '255', 'style': 'width:8em' });
		const pwmInvertedInput = E('input', { 'type': 'checkbox', 'checked': true });
		const pwmStartupInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '128', 'min': '-1', 'max': '255', 'style': 'width:8em' });
		const rampUpInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '25', 'min': '1', 'max': '255', 'style': 'width:8em' });
		const rampDownInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '8', 'min': '1', 'max': '255', 'style': 'width:8em' });
		const hystInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '2000', 'min': '0', 'max': '100000', 'style': 'width:8em' });
		const policyInput = E('select', { 'class': 'cbi-input-select', 'style': 'min-width:8em' }, [
			E('option', { 'value': 'max', 'selected': true }, _('max'))
		]);
		const failsafeInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '64', 'min': '0', 'max': '255', 'style': 'width:8em' });
		const pidfileInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': '/var/run/fancontrol.pid', 'style': 'min-width:16em' });
		const restartInput = E('input', { 'type': 'checkbox', 'checked': true });
		const controlModeInput = E('select', { 'class': 'cbi-input-select', 'style': 'min-width:10em' }, [
			E('option', { 'value': 'fancontrol' }, _('fancontrol')),
			E('option', { 'value': 'kernel' }, _('kernel auto'))
		]);
		const modeSwitchButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em',
			'click': ui.createHandlerFn(this, function() {
				return this.handleSwitchControlMode(refs);
			})
		}, _('Switch Mode'));
		const modeStatus = E('div', { 'class': 'cbi-value-description', 'style': 'margin-top:0.4em' }, '');

		const tbody = E('tbody', []);
		const defaults = defaultSources(channels);
		this.fillRows(defaults, tbody);

		const refs = {
			intervalInput,
			outputInput,
			pwmPathInput,
			pwmEnableInput,
			thermalModePathInput,
			pwmMinInput,
			pwmMaxInput,
			pwmInvertedInput,
			pwmStartupInput,
			rampUpInput,
			rampDownInput,
			hystInput,
			policyInput,
			failsafeInput,
			pidfileInput,
			restartInput,
			controlModeInput,
			modeStatus,
			tbody
		};

		this.applyLoadedBoardConfig(loadedBoard, refs, false);
		this.applyControlModeInfo(controlState, refs);

		const addSysfsBtn = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'click': ui.createHandlerFn(this, function() {
				this.createSourceRow({
					enabled: true,
					id: 'sysfs' + (this._sourceRows.length + 1),
					type: 'sysfs',
					path: '/sys/class/thermal/thermal_zone0/temp',
					args: '{}',
					t_start: 60000,
					t_full: 80000,
					t_crit: 90000,
					ttl: 6,
					poll: 2,
					weight: 100
				}, tbody);
			})
		}, _('Add Sysfs Source'));

		const addUbusBtn = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em',
			'click': ui.createHandlerFn(this, function() {
				this.createSourceRow({
					enabled: true,
					id: 'rm500',
					type: 'ubus',
					object: 'qmodem',
					method: 'get_temperature',
					key: 'temp_mC',
					args: '{"config_section":"modem1"}',
					t_start: 58000,
					t_full: 76000,
					t_crit: 85000,
					ttl: 20,
					poll: 10,
					weight: 130
				}, tbody);
			})
		}, _('Add Ubus Source'));

		const generateButton = E('button', {
			'class': 'btn cbi-button cbi-button-apply',
			'type': 'button',
			'click': ui.createHandlerFn(this, function() {
				return this.handleGenerate(refs);
			})
		}, _('Generate /etc/fancontrol.r3mini'));

		const loadButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em',
			'click': ui.createHandlerFn(this, function() {
				return this.handleLoadExisting(refs);
			})
		}, _('Load Existing Config'));

		const rescanButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em',
			'click': ui.createHandlerFn(this, function() {
				window.location.reload();
			})
		}, _('Rescan'));

		const desc = E('div', { 'class': 'cbi-section-descr' }, [
			E('p', _('BPI R3 Mini board-mode editor for fancontrol. It writes /etc/fancontrol.r3mini with multiple thermal sources (SoC/NVMe/RM500).')),
			E('p', _('Keep only one writer for the same PWM channel to avoid conflicts with kernel auto fan control.'))
		]);

			return E('div', { 'class': 'cbi-map' }, [
				E('h2', _('Fan Control Board Editor')),
				desc,
				E('div', { 'class': 'cbi-section' }, [
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Output file')), outputInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Interval (s)')), intervalInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM path')), pwmPathInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM enable path')), pwmEnableInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Thermal mode path')), thermalModePathInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM min/max')), pwmMinInput, E('span', { 'style': 'padding:0 0.6em' }, '/'), pwmMaxInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM inverted (255=min speed, 0=max speed)')), pwmInvertedInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM startup threshold (-1 disable)')), pwmStartupInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Ramp up/down')), rampUpInput, E('span', { 'style': 'padding:0 0.6em' }, '/'), rampDownInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Hysteresis (mC)')), hystInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Policy')), policyInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Failsafe PWM')), failsafeInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PID file')), pidfileInput ]),
					E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Restart service')), restartInput ]),
					E('div', { 'style': 'margin-bottom:0.9em' }, [
						E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Control mode')),
						controlModeInput,
						modeSwitchButton,
						modeStatus
					])
				]),
				E('div', { 'class': 'cbi-section' }, [
				E('h3', _('Thermal Sources')),
				E('div', { 'style': 'margin-bottom:0.8em' }, [ addSysfsBtn, addUbusBtn ]),
				E('div', { 'class': 'table', 'style': 'overflow:auto' }, [
					E('table', { 'class': 'table cbi-section-table' }, [
						E('thead', [ E('tr', [
							E('th', _('Use')),
							E('th', _('ID')),
							E('th', _('Type')),
							E('th', _('Sysfs Path')),
							E('th', _('Ubus Object')),
							E('th', _('Method')),
							E('th', _('Key')),
							E('th', _('Args JSON')),
							E('th', _('T_START(mC)')),
							E('th', _('T_FULL(mC)')),
							E('th', _('T_CRIT(mC)')),
							E('th', _('TTL(s)')),
							E('th', _('POLL(s)')),
							E('th', _('Weight')),
							E('th', _('Action'))
						]) ]),
						tbody
					])
				])
			]),
			E('div', { 'class': 'cbi-page-actions' }, [
				generateButton,
				loadButton,
				rescanButton
			])
		]);
	},

	handleSave: null,
	handleSaveApply: null,
	handleReset: null
});
