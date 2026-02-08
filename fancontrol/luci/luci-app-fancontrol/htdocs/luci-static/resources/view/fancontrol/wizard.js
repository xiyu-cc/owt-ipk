'use strict';
'require rpc';
'require ui';
'require view';

const callScan = rpc.declare({
	object: 'luci.fancontrol',
	method: 'scan'
});

const callLoadConfig = rpc.declare({
	object: 'luci.fancontrol',
	method: 'loadConfig',
	params: [ 'path' ]
});

const callApply = rpc.declare({
	object: 'luci.fancontrol',
	method: 'apply',
	params: [ 'output', 'interval', 'entries' ]
});

const callServiceAction = rpc.declare({
	object: 'luci.fancontrol',
	method: 'serviceAction',
	params: [ 'action' ]
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

return view.extend({
	load: function() {
		return Promise.all([
			callScan(),
			callLoadConfig('/etc/fancontrol')
		]);
	},

	applyLoadedConfig: function(cfg, intervalInput, notifyResult) {
		if (!cfg || !cfg.ok)
			return;

		if (cfg.interval != null && intervalInput)
			intervalInput.value = String(intInRange(cfg.interval, 10, 1, 3600));

		if (!cfg.exists) {
			if (notifyResult)
				ui.addNotification(null, E('p', _('No readable config file at %s').format(cfg.path || '/etc/fancontrol')), 'info');
			return;
		}

		const mapped = {};
		const entries = Array.isArray(cfg.entries) ? cfg.entries : [];

		for (let i = 0; i < entries.length; i++) {
			const e = entries[i] || {};
			if (e.pwm_rel)
				mapped[e.pwm_rel] = e;
		}

		let matched = 0;
		for (let i = 0; i < this._rows.length; i++) {
			const row = this._rows[i];
			const e = mapped[row.pwm_rel];

			if (!e) {
				row.enabled.checked = false;
				continue;
			}

			matched++;
			row.enabled.checked = true;
			row.temp.value = e.temp_rel != null ? String(e.temp_rel) : row.temp.value;
			row.fan.value = e.fan_rel != null ? String(e.fan_rel) : '';
			row.mintemp.value = String(intInRange(e.mintemp, 45, -100, 200));
			row.maxtemp.value = String(intInRange(e.maxtemp, 65, -100, 250));
			row.minstart.value = String(intInRange(e.minstart, 150, 0, 255));
			row.minstop.value = String(intInRange(e.minstop, 80, 0, 255));
			row.minpwm.value = String(intInRange(e.minpwm, 0, 0, 255));
			row.maxpwm.value = String(intInRange(e.maxpwm, 255, 0, 255));
			row.average.value = String(intInRange(e.average, 1, 1, 120));
			row.devpath_rel = e.devpath_rel != null ? String(e.devpath_rel) : row.devpath_rel;
			row.devname = e.devname != null ? String(e.devname) : row.devname;
		}

		if (notifyResult)
			ui.addNotification(null, E('p', _('Loaded %d channel(s) from %s').format(matched, cfg.path || '/etc/fancontrol')), 'info');
	},

	handleLoadExisting: async function(intervalInput, outputInput) {
		const path = (outputInput.value || '/etc/fancontrol').trim() || '/etc/fancontrol';
		const cfg = await callLoadConfig(path);

		if (!cfg || !cfg.ok) {
			ui.addNotification(null, E('p', _('Failed to load config: %s').format(cfg ? (cfg.error || 'unknown error') : 'rpc failed')), 'danger');
			return;
		}

		this.applyLoadedConfig(cfg, intervalInput, true);
	},

	handleGenerate: async function(intervalInput, outputInput, restartInput) {
		const interval = intInRange(intervalInput.value, 10, 1, 3600);
		const output = (outputInput.value || '/etc/fancontrol').trim() || '/etc/fancontrol';
		const lines = [];
		let active = 0;

		for (let i = 0; i < this._rows.length; i++) {
			const row = this._rows[i];
			const enabled = row.enabled.checked ? '1' : '0';
			const mintemp = intInRange(row.mintemp.value, 45, -100, 200);
			let maxtemp = intInRange(row.maxtemp.value, 65, -100, 250);
			const minstart = intInRange(row.minstart.value, 150, 0, 255);
			let minstop = intInRange(row.minstop.value, 80, 0, 255);
			const minpwm = intInRange(row.minpwm.value, 0, 0, 255);
			const maxpwm = intInRange(row.maxpwm.value, 255, 0, 255);
			const average = intInRange(row.average.value, 1, 1, 120);

			if (mintemp >= maxtemp)
				maxtemp = mintemp + 10;
			if (minstop >= maxpwm)
				minstop = Math.max(0, maxpwm - 1);
			if (minstop < minpwm)
				minstop = minpwm;

			if (enabled === '1')
				active++;

			lines.push([
				enabled,
				safeField(row.pwm_rel),
				safeField(row.temp.value),
				safeField(row.fan.value),
				String(mintemp),
				String(maxtemp),
				String(minstart),
				String(minstop),
				String(minpwm),
				String(maxpwm),
				String(average),
				safeField(row.devpath_rel),
				safeField(row.devname)
			].join(';'));
		}

		if (!active) {
			ui.addNotification(null, E('p', _('Please enable at least one PWM channel.')), 'danger');
			return;
		}

		const result = await callApply(output, String(interval), lines.join('\n'));
		if (!result || !result.ok) {
			ui.addNotification(null, E('p', _('Failed to generate config: %s').format(result ? (result.error || 'unknown error') : 'rpc failed')), 'danger');
			return;
		}

		if (restartInput.checked) {
			const svc = await callServiceAction('restart');
			if (!svc || !svc.ok) {
				ui.addNotification(null, E('p', _('Config saved, but restarting fancontrol failed.')), 'danger');
				return;
			}
		}

		ui.addNotification(null, E('p', _('Configuration written to %s').format(result.output || output)), 'info');
	},

	render: function(data) {
		const scan = data[0] || {};
		const loaded = data[1] || {};
		const channels = Array.isArray(scan.channels) ? scan.channels : [];
		this._rows = [];

		const intervalInput = E('input', {
			'class': 'cbi-input-text',
			'type': 'number',
			'min': '1',
			'max': '3600',
			'value': '10',
			'style': 'width:8em'
		});

		const outputInput = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'value': '/etc/fancontrol',
			'style': 'min-width:24em'
		});

		const restartInput = E('input', {
			'type': 'checkbox',
			'checked': true
		});

		const header = E('tr', [
			E('th', _('Use')),
			E('th', _('PWM')),
			E('th', _('Temperature Source')),
			E('th', _('Fan Feedback')),
			E('th', _('MINTEMP')),
			E('th', _('MAXTEMP')),
			E('th', _('MINSTART')),
			E('th', _('MINSTOP')),
			E('th', _('MINPWM')),
			E('th', _('MAXPWM')),
			E('th', _('AVERAGE'))
		]);

		const rows = channels.map((c) => {
			const enabled = E('input', { 'type': 'checkbox', 'checked': true });
			const temp = E('input', {
				'class': 'cbi-input-text',
				'type': 'text',
				'value': c.temp_rel || '',
				'style': 'min-width:13em'
			});
			const fan = E('input', {
				'class': 'cbi-input-text',
				'type': 'text',
				'value': c.fan_rel || '',
				'placeholder': '-',
				'style': 'min-width:13em'
			});
			const mintemp = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '45', 'style': 'width:6em' });
			const maxtemp = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '65', 'style': 'width:6em' });
			const minstart = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '150', 'style': 'width:6em' });
			const minstop = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '80', 'style': 'width:6em' });
			const minpwm = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '0', 'style': 'width:6em' });
			const maxpwm = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '255', 'style': 'width:6em' });
			const average = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '1', 'style': 'width:6em' });

			this._rows.push({
				enabled,
				temp,
				fan,
				mintemp,
				maxtemp,
				minstart,
				minstop,
				minpwm,
				maxpwm,
				average,
				pwm_rel: c.pwm_rel || '',
				devpath_rel: c.devpath_rel || '',
				devname: c.devname || c.hwmon_name || ''
			});

			return E('tr', [
				E('td', enabled),
				E('td', [ E('code', c.pwm_rel || '-') ]),
				E('td', temp),
				E('td', fan),
				E('td', mintemp),
				E('td', maxtemp),
				E('td', minstart),
				E('td', minstop),
				E('td', minpwm),
				E('td', maxpwm),
				E('td', average)
			]);
		});

		const generateButton = E('button', {
			'class': 'btn cbi-button cbi-button-apply',
			'type': 'button',
			'click': ui.createHandlerFn(this, function() {
				return this.handleGenerate(intervalInput, outputInput, restartInput);
			})
		}, _('Generate /etc/fancontrol'));

		const loadButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em',
			'click': ui.createHandlerFn(this, function() {
				return this.handleLoadExisting(intervalInput, outputInput);
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
			E('p', _('This page replaces interactive pwmconfig. It scans writable PWM channels and writes a fancontrol configuration file.')),
			E('p', _('Tip: keep only one controller for each PWM channel (kernel auto control or fancontrol).'))
		]);

		if (!rows.length) {
			return E('div', { 'class': 'cbi-map' }, [
				E('h2', _('Fan Control Wizard')),
				desc,
				E('div', { 'class': 'cbi-section' }, [
					E('p', _('No writable PWM channels were found under /sys/class/hwmon.'))
				]),
				E('div', { 'class': 'cbi-page-actions' }, [ rescanButton ])
			]);
		}

		this.applyLoadedConfig(loaded, intervalInput, false);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', _('Fan Control Wizard')),
			desc,
			E('div', { 'class': 'cbi-section' }, [
				E('div', { 'style': 'margin-bottom:0.8em' }, [
					E('label', { 'style': 'display:inline-block; min-width:11em' }, _('Update interval (s)')),
					intervalInput
				]),
				E('div', { 'style': 'margin-bottom:0.8em' }, [
					E('label', { 'style': 'display:inline-block; min-width:11em' }, _('Output file')),
					outputInput
				]),
				E('div', { 'style': 'margin-bottom:0.8em' }, [
					E('label', { 'style': 'display:inline-block; min-width:11em' }, _('Restart service')),
					restartInput
				]),
				E('div', { 'class': 'table', 'style': 'overflow:auto' }, [
					E('table', { 'class': 'table cbi-section-table' }, [
						E('thead', [ header ]),
						E('tbody', rows)
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
