'use strict';
'require ui';
'require fancontrol.rpc as fanRpc';
'require fancontrol.validators as validators';
'require fancontrol.sources-table as sourceTableLib';

const spec = {
	load: function() {
		return fanRpc.loadInitial();
	},

	bindButtonClick: function(btn, fn) {
		btn.addEventListener('click', (ev) => {
			if (ev) {
				ev.preventDefault();
				ev.stopPropagation();
			}
			return fn.call(this, ev);
		});
	},

	formatRuntimeStatus: function(status) {
		if (!status || !status.ok)
			return _('运行遥测不可用。');
		if (status.info)
			return validators.safeField(status.info);

		const pwm = status.pwm || {};
		const safety = status.safety || {};
		const list = Array.isArray(status.sources) ? status.sources : [];
		const lines = [];

		lines.push(_('PWM 当前/目标/已应用: %s / %s / %s').format(
			String(pwm.current != null ? pwm.current : '-'),
			String(pwm.target != null ? pwm.target : '-'),
			String(pwm.applied != null ? pwm.applied : '-')
		));
		lines.push(_('安全状态 any_valid/any_timeout/critical: %s / %s / %s').format(
			String(safety.any_valid != null ? safety.any_valid : '-'),
			String(safety.any_timeout != null ? safety.any_timeout : '-'),
			String(safety.critical != null ? safety.critical : '-')
		));

		for (let i = 0; i < list.length; i++) {
			const s = list[i] || {};
			lines.push(
				'%s: ok=%s temp=%s age=%ss stale=%s active=%s err=%s'.format(
					validators.safeField(s.id || ('source' + (i + 1))),
					String(s.ok != null ? s.ok : '-'),
					String(s.temp_mC != null ? s.temp_mC : '-'),
					String(s.age_s != null ? s.age_s : '-'),
					String(s.stale != null ? s.stale : '-'),
					String(s.active != null ? s.active : '-'),
					validators.safeField(s.error || '-')
				)
			);
		}

		return lines.join('\n');
	},

	applyRuntimeStatus: function(status, refs) {
		if (!refs || !refs.runtimeStatus)
			return;

		refs.runtimeStatus.textContent = this.formatRuntimeStatus(status);
		if (this._sourceTable)
			this._sourceTable.applySourceRuntime(status);
	},

	refreshRuntimeStatus: async function(refs, notifyResult) {
		try {
			const status = await fanRpc.runtimeStatus();
			if (!status || !status.ok) {
				this.applyRuntimeStatus(status || {}, refs);
				if (notifyResult)
					ui.addNotification(null, E('p', _('刷新运行遥测失败。')), 'danger');
				return;
			}
			this.applyRuntimeStatus(status, refs);
			if (notifyResult)
				ui.addNotification(null, E('p', _('运行遥测已刷新。')), 'info');
		} catch (e) {
			this.applyRuntimeStatus({}, refs);
			if (notifyResult)
				ui.addNotification(null, E('p', _('刷新运行遥测失败。')), 'danger');
		}
	},

	startRuntimeAutoRefresh: function(refs) {
		if (this._runtimeTimer) {
			clearInterval(this._runtimeTimer);
			this._runtimeTimer = null;
		}
		this._runtimeTimer = setInterval(() => {
			this.refreshRuntimeStatus(refs, false);
		}, 2000);
		this._runtimeUnloadHandler = () => {
			if (this._runtimeTimer) {
				clearInterval(this._runtimeTimer);
				this._runtimeTimer = null;
			}
		};
		window.addEventListener('beforeunload', this._runtimeUnloadHandler, { once: true });
	},

	applyLoadedBoardConfig: function(cfg, refs, notifyResult) {
		if (!cfg || !cfg.ok)
			return false;

		refs.intervalInput.value = String(cfg.interval != null ? cfg.interval : '');
		refs.modeSelect.value = (validators.safeField(cfg.control_mode).toLowerCase() === 'user') ? 'user' : 'kernel';
		refs.pwmPathInput.value = validators.safeField(cfg.pwm_path || '');
		refs.pwmEnableInput.value = validators.safeField(cfg.pwm_enable_path || '');
		refs.controlModePathInput.value = validators.safeField(cfg.control_mode_path || '');
		refs.pwmMinInput.value = String(cfg.pwm_min != null ? cfg.pwm_min : '');
		refs.pwmMaxInput.value = String(cfg.pwm_max != null ? cfg.pwm_max : '');
		refs.rampUpInput.value = String(cfg.ramp_up != null ? cfg.ramp_up : '');
		refs.rampDownInput.value = String(cfg.ramp_down != null ? cfg.ramp_down : '');
		refs.hystInput.value = String(cfg.hysteresis_mC != null ? cfg.hysteresis_mC : '');
		refs.failsafeInput.value = String(cfg.failsafe_pwm != null ? cfg.failsafe_pwm : '');

		const src = Array.isArray(cfg.sources) ? cfg.sources : [];
		this._sourceTable.fillRows(src, refs.tbody);

		if (notifyResult) {
			if (cfg.exists)
				ui.addNotification(null, E('p', _('已从 %s 载入 %d 个温度源').format(cfg.path || this._configPath || '-', src.length)), 'info');
			else
				ui.addNotification(null, E('p', _('配置文件不存在，已载入后端默认配置（%d 个温度源）').format(src.length)), 'info');
		}
		return true;
	},

	prepareDefaultSources: function(hasQmodem) {
		const src = Array.isArray(this._defaultSources) ? this._defaultSources : [];
		const out = [];

		for (let i = 0; i < src.length; i++) {
			const item = Object.assign({}, src[i] || {});
			if (!hasQmodem && String(item.type || '').toLowerCase() === 'ubus' &&
				String(item.object || '').toLowerCase() === 'qmodem') {
				item.enabled = false;
			}
			out.push(item);
		}
		return out;
	},

	sourceTemplate: function(type, hasQmodem) {
		const t = (this._sourceTemplates && this._sourceTemplates[type]) ? this._sourceTemplates[type] : null;
		if (t) {
			const out = Object.assign({}, t);
			if (type === 'ubus' && !hasQmodem)
				out.enabled = false;
			return out;
		}

		const list = this.prepareDefaultSources(hasQmodem);
		for (let i = 0; i < list.length; i++) {
			const s = list[i] || {};
			if (String(s.type || '').toLowerCase() === type)
				return Object.assign({}, s);
		}

		return {
			enabled: (type !== 'ubus') ? true : hasQmodem,
			type: type,
			args: ''
		};
	},

	resetToDefaults: function(refs, hasQmodem) {
		if (!this._backendDefaults || !this._backendDefaults.ok) {
			ui.addNotification(null, E('p', _('后端默认配置不可用。')), 'danger');
			return;
		}

		const cfg = Object.assign({}, this._backendDefaults);
		cfg.sources = this.prepareDefaultSources(hasQmodem);
		this.applyLoadedBoardConfig(cfg, refs, false);
		this.refreshRuntimeStatus(refs, false);
		ui.addNotification(null, E('p', _('已恢复后端默认配置，请点击“应用”。')), 'info');
	},

	handleLoadExisting: async function(refs) {
		try {
			const cfg = await fanRpc.loadBoardConfig();
			if (!cfg || !cfg.ok) {
				ui.addNotification(null, E('p', _('读取配置失败: %s').format(cfg ? (cfg.error || '未知错误') : 'RPC 调用失败')), 'danger');
				return;
			}

			this.applyLoadedBoardConfig(cfg, refs, true);
			await this.refreshRuntimeStatus(refs, false);
		} catch (e) {
			ui.addNotification(null, E('p', _('读取配置失败: RPC 调用失败')), 'danger');
		}
	},

	handleApply: async function(refs) {
		try {
			const sources = this._sourceTable.collectSources();
			const result = await fanRpc.applyBoardConfig({
				interval: validators.safeField(refs.intervalInput.value),
				control_mode: validators.safeField(refs.modeSelect.value),
				pwm_path: validators.safeField(refs.pwmPathInput.value),
				pwm_enable_path: validators.safeField(refs.pwmEnableInput.value),
				control_mode_path: validators.safeField(refs.controlModePathInput.value),
				pwm_min: validators.safeField(refs.pwmMinInput.value),
				pwm_max: validators.safeField(refs.pwmMaxInput.value),
				ramp_up: validators.safeField(refs.rampUpInput.value),
				ramp_down: validators.safeField(refs.rampDownInput.value),
				hysteresis_mC: validators.safeField(refs.hystInput.value),
				failsafe_pwm: validators.safeField(refs.failsafeInput.value),
				sources: sources
			});

			if (!result || !result.ok) {
				ui.addNotification(null, E('p', _('应用失败: %s').format(result ? (result.error || '未知错误') : 'RPC 调用失败')), 'danger');
				return;
			}

			const svc = await fanRpc.serviceAction('restart');
			if (!svc || !svc.ok) {
				ui.addNotification(null, E('p', _('配置已保存，但重启 fancontrol 失败。')), 'danger');
				return;
			}

			await this.refreshRuntimeStatus(refs, false);
			ui.addNotification(null, E('p', _('配置已写入 %s').format(this._configPath || '-')), 'info');
		} catch (e) {
			ui.addNotification(null, E('p', _('应用失败: RPC 调用失败')), 'danger');
		}
	},

	render: function(data) {
		const state = data || {};
		const loadedBoard = state.loadedBoard || {};
		const boardSchema = state.boardSchema || {};
		const runtimeState = state.runtimeState || {};
		const hasQmodem = state.hasQmodem === true;

		const constants = (boardSchema && boardSchema.constants) ? boardSchema.constants : {};
		const schemaDefaults = (boardSchema && boardSchema.defaults) ? boardSchema.defaults : {};
		const sourceInfo = (boardSchema && boardSchema.source) ? boardSchema.source : {};
		const sourceTemplates = (sourceInfo && sourceInfo.templates) ? sourceInfo.templates : {};

		this._sourceTable = sourceTableLib.create();
		this._configPath = validators.safeField(constants.config_path || loadedBoard.path || '');
		this._sourceTemplates = sourceTemplates;
		this._defaultSources = Array.isArray(schemaDefaults.sources) ? schemaDefaults.sources : [];
		this._backendDefaults = (boardSchema && boardSchema.ok && schemaDefaults) ?
			Object.assign({ ok: 1, exists: 0, path: this._configPath }, schemaDefaults) : null;

		const intervalInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '', 'style': 'width:8em' });
		const pwmPathInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': '', 'style': 'min-width:24em' });
		const pwmEnableInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': '', 'style': 'min-width:24em' });
		const controlModePathInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': '', 'style': 'min-width:24em' });
		const pwmMinInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '', 'style': 'width:8em' });
		const pwmMaxInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '', 'style': 'width:8em' });
		const rampUpInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '', 'style': 'width:8em' });
		const rampDownInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '', 'style': 'width:8em' });
		const hystInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '', 'style': 'width:8em' });
		const failsafeInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '', 'style': 'width:8em' });
		const modeSelect = E('select', { 'class': 'cbi-input-select', 'style': 'width:10em' }, [
			E('option', { 'value': 'kernel' }, _('内核')),
			E('option', { 'value': 'user' }, _('用户'))
		]);

		const runtimeStatus = E('pre', {
			'class': 'cbi-value-description',
			'style': 'margin-top:0.4em; white-space:pre-wrap; max-height:16em; overflow:auto;'
		}, _('运行遥测不可用。'));

		const tbody = E('tbody', []);

		const addSysfsBtn = E('button', {
			'class': 'btn cbi-button',
			'type': 'button'
		}, _('新增 Sysfs 温度源'));

		const addUbusBtn = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em'
		}, _('新增 Ubus 温度源'));

		const applyButton = E('button', {
			'class': 'btn cbi-button cbi-button-apply',
			'type': 'button'
		}, _('应用'));

		const loadButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em'
		}, _('读取现有配置'));

		const rescanButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em'
		}, _('重新扫描'));

		const refreshTelemetryButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em'
		}, _('刷新遥测'));

		const resetDefaultsButton = E('button', {
			'class': 'btn cbi-button',
			'type': 'button',
			'style': 'margin-left:0.5em'
		}, _('一键重置配置'));

		const refs = {
			intervalInput,
			pwmPathInput,
			pwmEnableInput,
			controlModePathInput,
			pwmMinInput,
			pwmMaxInput,
			rampUpInput,
			rampDownInput,
			hystInput,
			failsafeInput,
			modeSelect,
			runtimeStatus,
			tbody
		};

		this.bindButtonClick(addSysfsBtn, function() {
			const tpl = this.sourceTemplate('sysfs', hasQmodem);
			const sidBase = validators.safeField(tpl.id || 'sysfs') || 'sysfs';
			tpl.id = this._sourceTable.nextUniqueId(sidBase);
			tpl.enabled = true;
			this._sourceTable.createSourceRow(tpl, tbody);
		});

		this.bindButtonClick(addUbusBtn, function() {
			const tpl = this.sourceTemplate('ubus', hasQmodem);
			const sidBase = validators.safeField(tpl.id || 'ubus') || 'ubus';
			tpl.id = this._sourceTable.nextUniqueId(sidBase);
			tpl.enabled = hasQmodem;
			this._sourceTable.createSourceRow(tpl, tbody);
		});

		this.bindButtonClick(applyButton, function() {
			return this.handleApply(refs);
		});
		this.bindButtonClick(loadButton, function() {
			return this.handleLoadExisting(refs);
		});
		this.bindButtonClick(rescanButton, function() {
			window.location.reload();
		});
		this.bindButtonClick(refreshTelemetryButton, function() {
			return this.refreshRuntimeStatus(refs, true);
		});
		this.bindButtonClick(resetDefaultsButton, function() {
			return this.resetToDefaults(refs, hasQmodem);
		});

		const loadFailureLabels = {
			scan: _('硬件扫描'),
			loadedBoard: _('读取配置'),
			boardSchema: _('读取后端 schema'),
			runtimeState: _('读取遥测'),
			hasQmodem: _('探测 qmodem')
		};
		if (Array.isArray(state.failures) && state.failures.length > 0) {
			const labels = [];
			for (let i = 0; i < state.failures.length; i++) {
				const key = state.failures[i];
				labels.push(loadFailureLabels[key] || String(key));
			}
			ui.addNotification(null, E('p', _('部分初始化请求失败: %s。').format(labels.join(' / '))), 'warning');
		}
		if (!hasQmodem)
			ui.addNotification(null, E('p', _('未探测到 qmodem，默认已禁用 qmodem 温度源。')), 'info');

		let loadedApplied = this.applyLoadedBoardConfig(loadedBoard, refs, false);
		if (!loadedApplied && this._backendDefaults)
			loadedApplied = this.applyLoadedBoardConfig(this._backendDefaults, refs, false);
		if (!loadedApplied)
			this._sourceTable.ensureRows(refs, this.prepareDefaultSources(hasQmodem));
		this.applyRuntimeStatus(runtimeState, refs);
		this.startRuntimeAutoRefresh(refs);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', _('风扇控制')),
			E('div', { 'class': 'cbi-section' }, [
				E('div', { 'style': 'margin-bottom:0.7em' }, [
					E('label', { 'style': 'display:inline-block; min-width:14em' }, _('模式')),
					modeSelect
				]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('控制周期 (秒)')), intervalInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM 路径')), pwmPathInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM 使能路径')), pwmEnableInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('控制模式路径')), controlModePathInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM_MIN')), pwmMinInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM_MAX')), pwmMaxInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('升速/降速 (秒)')), rampUpInput, E('span', { 'style': 'padding:0 0.6em' }, '/'), rampDownInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('抖动 (m℃)')), hystInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('失效保护 PWM')), failsafeInput ]),
				E('div', { 'style': 'margin-bottom:0.9em' }, [
					E('label', { 'style': 'display:inline-block; min-width:14em; vertical-align:top' }, _('运行遥测')),
					runtimeStatus
				])
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', _('温度源')),
				E('div', { 'style': 'margin-bottom:0.8em; display:flex; flex-wrap:wrap; gap:0.5em' }, [ addSysfsBtn, addUbusBtn ]),
				E('div', { 'style': 'overflow:auto' }, [
					E('table', { 'class': 'table cbi-section-table' }, [
						E('thead', [ E('tr', { 'class': 'tr cbi-section-table-titles' }, [
							E('th', { 'class': 'th cbi-section-table-cell' }, _('启用')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('ID')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('温度')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('类型')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('Sysfs 路径')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('Ubus 对象')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('方法')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('键名')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('参数 JSON')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('T_START(mC)')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('T_FULL(mC)')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('T_CRIT(mC)')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('TTL(s)')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('POLL(s)')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('权重')),
							E('th', { 'class': 'th cbi-section-table-cell' }, _('操作'))
						]) ]),
						tbody
					])
				])
			]),
			E('div', { 'class': 'cbi-page-actions', 'style': 'display:flex; flex-wrap:wrap; gap:0.5em' }, [
				applyButton,
				loadButton,
				rescanButton,
				refreshTelemetryButton,
				resetDefaultsButton
			])
		]);
	},

	handleSave: null,
	handleSaveApply: null,
	handleReset: null
};

return L.Class.extend({
	getSpec: function() {
		return spec;
	}
});
