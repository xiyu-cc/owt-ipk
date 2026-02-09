'use strict';
'require ui';
'require fancontrol.rpc as fanRpc';
'require fancontrol.validators as validators';
'require fancontrol.sources-table as sourceTableLib';

const spec = {
	load: function() {
		return fanRpc.loadInitial(validators.DEFAULTS.CONFIG_PATH);
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

	setSubmitBusy: function(refs, busy) {
		this._submitBusy = !!busy;
		const controls = (refs && Array.isArray(refs.submitControls)) ? refs.submitControls : [];
		for (let i = 0; i < controls.length; i++) {
			const ctl = controls[i];
			if (ctl)
				ctl.disabled = !!busy;
		}
		if (refs && refs.submitStatus)
			refs.submitStatus.textContent = busy ? _('提交中，请稍候...') : '';
	},

	runSubmitAction: function(refs, fn) {
		if (this._submitBusy)
			return Promise.resolve(false);

		this.setSubmitBusy(refs, true);
		return Promise.resolve()
			.then(fn)
			.finally(() => {
				this.setSubmitBusy(refs, false);
			});
	},

	formatModeStatus: function(info) {
		const mode = validators.safeField(info && info.mode ? info.mode : 'kernel').toLowerCase();
		const modeLabel = mode === 'fancontrol' ? '用户态 fancontrol' : '内核自动';
		const running = info && info.running ? '是' : '否';
		const enabled = info && info.enabled ? '是' : '否';
		const thermalModePath = validators.safeField(info && info.thermal_mode_path ? info.thermal_mode_path : '-');
		const thermalMode = validators.safeField(info && info.thermal_mode ? info.thermal_mode : '-');

		return _('模式: %s | 服务运行: %s | 开机自启: %s | thermal 模式: %s (%s)')
			.format(modeLabel, running, enabled, thermalMode, thermalModePath);
	},

	applyControlModeInfo: function(info, refs) {
		if (!refs)
			return;

		const mode = validators.safeField(info && info.mode ? info.mode : 'kernel').toLowerCase();
		if (refs.fancontrolToggleInput)
			refs.fancontrolToggleInput.checked = (mode === 'fancontrol');
		if (refs.modeStatus)
			refs.modeStatus.textContent = this.formatModeStatus(info || {});
	},

	refreshControlMode: async function(refs, notifyResult) {
		try {
			const path = (refs.outputInput.value || validators.DEFAULTS.CONFIG_PATH).trim() || validators.DEFAULTS.CONFIG_PATH;
			const state = await fanRpc.getControlMode(path);

			if (!state || !state.ok) {
				if (notifyResult)
					ui.addNotification(null, E('p', _('查询控制模式失败。')), 'danger');
				return;
			}

			this.applyControlModeInfo(state, refs);
			if (notifyResult)
				ui.addNotification(null, E('p', _('当前控制模式: %s').format(validators.safeField(state.mode || 'kernel'))), 'info');
		} catch (e) {
			if (notifyResult)
				ui.addNotification(null, E('p', _('查询控制模式失败。')), 'danger');
		}
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
			const path = (refs && refs.outputInput && refs.outputInput.value)
				? ((refs.outputInput.value || validators.DEFAULTS.CONFIG_PATH).trim() || validators.DEFAULTS.CONFIG_PATH)
				: validators.DEFAULTS.CONFIG_PATH;
			const status = await fanRpc.runtimeStatus(path);
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

		if (!cfg.exists) {
			if (notifyResult)
				ui.addNotification(null, E('p', _('未读取到可用配置文件：%s').format(cfg.path || validators.DEFAULTS.CONFIG_PATH)), 'info');
			return false;
		}

		refs.intervalInput.value = String(validators.intInRange(cfg.interval, 1, 1, 3600));
		refs.pwmPathInput.value = validators.safeField(cfg.pwm_path || refs.pwmPathInput.value);
		refs.pwmEnableInput.value = validators.safeField(cfg.pwm_enable_path || refs.pwmEnableInput.value);
		refs.thermalModePathInput.value = validators.safeField(cfg.thermal_mode_path || refs.thermalModePathInput.value);
		const cfgMin = validators.intInRange(cfg.pwm_min, 0, 0, 255);
		const cfgMax = validators.intInRange(cfg.pwm_max, 255, 0, 255);
		const cfgInverted = !!cfg.pwm_inverted;
		const logicalMin = cfgInverted ? cfgMax : cfgMin;
		const logicalMax = cfgInverted ? cfgMin : cfgMax;
		refs.pwmMinInput.value = String(validators.intInRange(logicalMin, 128, 0, 255));
		refs.pwmMaxInput.value = String(validators.intInRange(logicalMax, 0, 0, 255));
		refs.rampUpInput.value = String(validators.intInRange(cfg.ramp_up, 25, 1, 255));
		refs.rampDownInput.value = String(validators.intInRange(cfg.ramp_down, 8, 1, 255));
		refs.hystInput.value = String(validators.intInRange(cfg.hysteresis_mC, 2000, 0, 100000));
		refs.failsafeInput.value = String(validators.intInRange(cfg.failsafe_pwm, 64, 0, 255));
		refs.pidfileInput.value = validators.safeField(cfg.pidfile || validators.DEFAULTS.PIDFILE);

		const src = Array.isArray(cfg.sources) ? cfg.sources : [];
		this._sourceTable.fillRows(src, refs.tbody);

		if (notifyResult)
			ui.addNotification(null, E('p', _('已从 %s 载入 %d 个温度源').format(cfg.path || validators.DEFAULTS.CONFIG_PATH, src.length)), 'info');
		return true;
	},

	resetToDefaults: function(refs, channels, hasQmodem) {
		const pwmDefaults = validators.defaultPwmPaths(channels);

		refs.outputInput.value = validators.DEFAULTS.CONFIG_PATH;
		refs.intervalInput.value = '1';
		refs.pwmPathInput.value = pwmDefaults.pwmPath;
		refs.pwmEnableInput.value = pwmDefaults.pwmEnablePath;
		refs.thermalModePathInput.value = validators.DEFAULTS.THERMAL_MODE_PATH;
		refs.pwmMinInput.value = '128';
		refs.pwmMaxInput.value = '0';
		refs.rampUpInput.value = '25';
		refs.rampDownInput.value = '8';
		refs.hystInput.value = '2000';
		refs.failsafeInput.value = '64';
		refs.pidfileInput.value = validators.DEFAULTS.PIDFILE;

		this._sourceTable.fillRows(validators.defaultSources(channels, hasQmodem), refs.tbody);
		refs.submitControls = refs.baseSubmitControls.concat(this._sourceTable.getAllButtons());
		this.refreshRuntimeStatus(refs, false);
		ui.addNotification(null, E('p', _('已恢复默认配置，请点击“生成 /etc/fancontrol.r3mini”。')), 'info');
	},

	handleSwitchControlMode: async function(refs, forcedMode) {
		try {
			let mode = validators.safeField(forcedMode || '').toLowerCase();
			if (mode !== 'fancontrol' && mode !== 'kernel')
				mode = (refs && refs.fancontrolToggleInput && refs.fancontrolToggleInput.checked) ? 'fancontrol' : 'kernel';
			const path = (refs.outputInput.value || validators.DEFAULTS.CONFIG_PATH).trim() || validators.DEFAULTS.CONFIG_PATH;
			const result = await fanRpc.setControlMode(mode, path);

			if (!result || !result.ok) {
				await this.refreshControlMode(refs, false);
				ui.addNotification(null, E('p', _('切换控制模式失败: %s').format(result ? (result.error || '未知错误') : 'RPC 调用失败')), 'danger');
				return;
			}

			this.applyControlModeInfo(result, refs);
			await this.refreshRuntimeStatus(refs, false);
			ui.addNotification(null, E('p', _('控制模式已切换为 %s').format(validators.safeField(result.mode || mode))), 'info');
		} catch (e) {
			await this.refreshControlMode(refs, false);
			ui.addNotification(null, E('p', _('切换控制模式失败: RPC 调用失败')), 'danger');
		}
	},

	handleToggleFancontrol: async function(refs) {
		const enabled = !!(refs && refs.fancontrolToggleInput && refs.fancontrolToggleInput.checked);
		const mode = enabled ? 'fancontrol' : 'kernel';
		return this.handleSwitchControlMode(refs, mode);
	},

	handleLoadExisting: async function(refs) {
		try {
			const path = (refs.outputInput.value || validators.DEFAULTS.CONFIG_PATH).trim() || validators.DEFAULTS.CONFIG_PATH;
			const cfg = await fanRpc.loadBoardConfig(path);

			if (!cfg || !cfg.ok) {
				ui.addNotification(null, E('p', _('读取配置失败: %s').format(cfg ? (cfg.error || '未知错误') : 'RPC 调用失败')), 'danger');
				return;
			}

			this.applyLoadedBoardConfig(cfg, refs, true);
			refs.submitControls = refs.baseSubmitControls.concat(this._sourceTable.getAllButtons());
			await this.refreshControlMode(refs, false);
			await this.refreshRuntimeStatus(refs, false);
		} catch (e) {
			ui.addNotification(null, E('p', _('读取配置失败: RPC 调用失败')), 'danger');
		}
	},

	handleGenerate: async function(refs) {
		try {
			const interval = validators.intInRange(refs.intervalInput.value, 1, 1, 3600);
			const output = (refs.outputInput.value || validators.DEFAULTS.CONFIG_PATH).trim() || validators.DEFAULTS.CONFIG_PATH;
			const pwmPath = validators.safeField(refs.pwmPathInput.value);
			const pwmEnablePath = validators.safeField(refs.pwmEnableInput.value);
			const thermalModePath = validators.safeField(refs.thermalModePathInput.value || validators.DEFAULTS.THERMAL_MODE_PATH);
			const pwmLogicalMin = validators.intInRange(refs.pwmMinInput.value, 128, 0, 255);
			const pwmLogicalMax = validators.intInRange(refs.pwmMaxInput.value, 0, 0, 255);
			const pwmInverted = (pwmLogicalMin > pwmLogicalMax) ? 1 : 0;
			const pwmMin = Math.min(pwmLogicalMin, pwmLogicalMax);
			const pwmMax = Math.max(pwmLogicalMin, pwmLogicalMax);
			const pwmStartup = -1;
			const rampUp = validators.intInRange(refs.rampUpInput.value, 25, 1, 255);
			const rampDown = validators.intInRange(refs.rampDownInput.value, 8, 1, 255);
			const hysteresis = validators.intInRange(refs.hystInput.value, 2000, 0, 100000);
			const policy = 'max';
			const failsafe = validators.intInRange(refs.failsafeInput.value, 64, 0, 255);
			const pidfile = validators.safeField(refs.pidfileInput.value || validators.DEFAULTS.PIDFILE) || validators.DEFAULTS.PIDFILE;

			if (!pwmPath) {
				ui.addNotification(null, E('p', _('PWM 路径不能为空。')), 'danger');
				return;
			}

			const pack = this._sourceTable.collectEntries();
			if (pack.active < 1) {
				ui.addNotification(null, E('p', _('至少启用一个温度源。')), 'danger');
				return;
			}
			if (pack.duplicates.length > 0) {
				ui.addNotification(null, E('p', _('SOURCE ID 不允许重复: %s').format(pack.duplicates.join(', '))), 'danger');
				return;
			}
			if (pack.errors.length > 0) {
				ui.addNotification(null, E('p', _('温度源配置无效: %s').format(pack.errors.join(' | '))), 'danger');
				return;
			}

			const result = await fanRpc.applyBoardConfig({
				output: output,
				interval: String(interval),
				pwm_path: pwmPath,
				pwm_enable_path: pwmEnablePath,
				thermal_mode_path: thermalModePath,
				pwm_min: String(pwmMin),
				pwm_max: String(pwmMax),
				pwm_inverted: String(pwmInverted),
				pwm_startup_pwm: String(pwmStartup),
				ramp_up: String(rampUp),
				ramp_down: String(rampDown),
				hysteresis_mC: String(hysteresis),
				policy: policy,
				failsafe_pwm: String(failsafe),
				pidfile: pidfile,
				entries: pack.lines.join('\n')
			});

			if (!result || !result.ok) {
				ui.addNotification(null, E('p', _('生成配置失败: %s').format(result ? (result.error || '未知错误') : 'RPC 调用失败')), 'danger');
				return;
			}

			const svc = await fanRpc.serviceAction('restart');
			if (!svc || !svc.ok) {
				ui.addNotification(null, E('p', _('配置已保存，但重启 fancontrol 失败。')), 'danger');
				return;
			}

			await this.refreshControlMode(refs, false);
			await this.refreshRuntimeStatus(refs, false);
			ui.addNotification(null, E('p', _('配置已写入 %s').format(result.output || output)), 'info');
		} catch (e) {
			ui.addNotification(null, E('p', _('生成配置失败: RPC 调用失败')), 'danger');
		}
	},

	render: function(data) {
		const state = data || {};
		const scan = state.scan || {};
		const loadedBoard = state.loadedBoard || {};
		const controlState = state.controlState || {};
		const runtimeState = state.runtimeState || {};
		const hasQmodem = state.hasQmodem === true;
		const channels = Array.isArray(scan.channels) ? scan.channels : [];
		const pwmDefaults = validators.defaultPwmPaths(channels);

		this._sourceTable = sourceTableLib.create();
		this._submitBusy = false;

		const intervalInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '1', 'min': '1', 'max': '3600', 'style': 'width:8em' });
		const outputInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': validators.DEFAULTS.CONFIG_PATH, 'style': 'min-width:24em' });
		const pwmPathInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': pwmDefaults.pwmPath, 'style': 'min-width:24em' });
		const pwmEnableInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': pwmDefaults.pwmEnablePath, 'style': 'min-width:24em' });
		const thermalModePathInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': validators.DEFAULTS.THERMAL_MODE_PATH, 'style': 'min-width:24em' });
		const pwmMinInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '128', 'min': '0', 'max': '255', 'style': 'width:8em' });
		const pwmMaxInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '0', 'min': '0', 'max': '255', 'style': 'width:8em' });
		const rampUpInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '25', 'min': '1', 'max': '255', 'style': 'width:8em' });
		const rampDownInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '8', 'min': '1', 'max': '255', 'style': 'width:8em' });
		const hystInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '2000', 'min': '0', 'max': '100000', 'style': 'width:8em' });
		const failsafeInput = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'value': '64', 'min': '0', 'max': '255', 'style': 'width:8em' });
		const pidfileInput = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'value': validators.DEFAULTS.PIDFILE, 'style': 'min-width:16em' });
		const fancontrolToggleInput = E('input', { 'type': 'checkbox' });
		const modeStatus = E('div', { 'class': 'cbi-value-description', 'style': 'margin-top:0.4em' }, '');
		const runtimeStatus = E('pre', {
			'class': 'cbi-value-description',
			'style': 'margin-top:0.4em; white-space:pre-wrap; max-height:16em; overflow:auto;'
		}, _('运行遥测不可用。'));
		const submitStatus = E('div', { 'class': 'cbi-value-description', 'style': 'margin-top:0.4em' }, '');

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

		const generateButton = E('button', {
			'class': 'btn cbi-button cbi-button-apply',
			'type': 'button'
		}, _('生成 /etc/fancontrol.r3mini'));

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
			outputInput,
			pwmPathInput,
			pwmEnableInput,
			thermalModePathInput,
			pwmMinInput,
			pwmMaxInput,
			rampUpInput,
			rampDownInput,
			hystInput,
			failsafeInput,
			pidfileInput,
			fancontrolToggleInput,
			modeStatus,
			runtimeStatus,
			submitStatus,
			tbody
		};

		refs.baseSubmitControls = [
			fancontrolToggleInput,
			addSysfsBtn,
			addUbusBtn,
			generateButton,
			loadButton,
			rescanButton,
			refreshTelemetryButton,
			resetDefaultsButton
		];
		refs.submitControls = refs.baseSubmitControls.slice();

		fancontrolToggleInput.addEventListener('change', () => {
			this.runSubmitAction(refs, () => this.handleToggleFancontrol(refs));
		});
		this.bindButtonClick(addSysfsBtn, function() {
			const sid = this._sourceTable.nextUniqueId('sysfs');
			this._sourceTable.createSourceRow({
				enabled: true,
				id: sid,
				type: 'sysfs',
				path: '/sys/class/thermal/thermal_zone0/temp',
				args: '{}',
				t_start: 60000,
				t_full: 80000,
				t_crit: 90000,
				ttl: 6,
				poll: 1,
				weight: 100
			}, tbody);
			refs.submitControls = refs.baseSubmitControls.concat(this._sourceTable.getAllButtons());
		});
		this.bindButtonClick(addUbusBtn, function() {
			const sid = this._sourceTable.nextUniqueId('rm500q-gl');
			this._sourceTable.createSourceRow({
				enabled: hasQmodem,
				id: sid,
				type: 'ubus',
				object: 'qmodem',
				method: 'get_temperature',
				key: 'temp_mC',
				args: '{"config_section":"' + validators.DEFAULTS.MODEM_SECTION + '"}',
				t_start: 58000,
				t_full: 76000,
				t_crit: 85000,
				ttl: 20,
				poll: 10,
				weight: 130
			}, tbody);
			refs.submitControls = refs.baseSubmitControls.concat(this._sourceTable.getAllButtons());
		});
		this.bindButtonClick(generateButton, function() {
			return this.runSubmitAction(refs, () => this.handleGenerate(refs));
		});
		this.bindButtonClick(loadButton, function() {
			return this.runSubmitAction(refs, () => this.handleLoadExisting(refs));
		});
		this.bindButtonClick(rescanButton, function() {
			window.location.reload();
		});
		this.bindButtonClick(refreshTelemetryButton, function() {
			return this.refreshRuntimeStatus(refs, true);
		});
		this.bindButtonClick(resetDefaultsButton, function() {
			return this.resetToDefaults(refs, channels, hasQmodem);
		});

		const loadFailureLabels = {
			scan: _('硬件扫描'),
			loadedBoard: _('读取配置'),
			controlState: _('读取模式'),
			runtimeState: _('读取遥测'),
			hasQmodem: _('探测 qmodem')
		};
		if (Array.isArray(state.failures) && state.failures.length > 0) {
			const labels = [];
			for (let i = 0; i < state.failures.length; i++) {
				const key = state.failures[i];
				labels.push(loadFailureLabels[key] || String(key));
			}
			ui.addNotification(null, E('p', _('部分初始化请求失败: %s。已使用降级默认值继续渲染页面。').format(labels.join(' / '))), 'warning');
		}
		if (!hasQmodem)
			ui.addNotification(null, E('p', _('未探测到 qmodem，默认已禁用 rm500q-gl 温度源。')), 'info');

		const loadedApplied = this.applyLoadedBoardConfig(loadedBoard, refs, false);
		if (!loadedApplied)
			this._sourceTable.ensureRows(refs, channels, hasQmodem);
		refs.submitControls = refs.baseSubmitControls.concat(this._sourceTable.getAllButtons());
		this.applyControlModeInfo(controlState, refs);
		this.applyRuntimeStatus(runtimeState, refs);
		this.startRuntimeAutoRefresh(refs);

		const desc = E('div', { 'class': 'cbi-section-descr' }, [
			E('p', _('BPI R3 Mini 板级风扇控制向导：写入 /etc/fancontrol.r3mini，支持 SoC / NVMe / RM500 多温度源。')),
			E('p', _('同一个 PWM 通道只能有一个控制者，避免与内核自动温控冲突。'))
		]);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', _('风扇控制板级编辑器')),
			desc,
			E('div', { 'class': 'cbi-section' }, [
				E('div', { 'style': 'margin-bottom:0.7em' }, [
					E('label', { 'style': 'display:inline-block; min-width:14em' }, _('启用')),
					fancontrolToggleInput
				]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('输出文件')), outputInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('控制周期 (秒)')), intervalInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM 路径')), pwmPathInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PWM 使能路径')), pwmEnableInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('Thermal 模式路径')), thermalModePathInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('最小 PWM (最低转速)')), pwmMinInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('最大 PWM (最高转速)')), pwmMaxInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('升速/降速步进')), rampUpInput, E('span', { 'style': 'padding:0 0.6em' }, '/'), rampDownInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('滞回 (mC)')), hystInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('失效保护 PWM')), failsafeInput ]),
				E('div', { 'style': 'margin-bottom:0.7em' }, [ E('label', { 'style': 'display:inline-block; min-width:14em' }, _('PID 文件')), pidfileInput ]),
				E('div', { 'style': 'margin-bottom:0.9em' }, [
					E('label', { 'style': 'display:inline-block; min-width:14em' }, _('当前模式状态')),
					modeStatus
				]),
				E('div', { 'style': 'margin-bottom:0.9em' }, [
					E('label', { 'style': 'display:inline-block; min-width:14em; vertical-align:top' }, _('运行遥测')),
					runtimeStatus
				]),
				E('div', { 'style': 'margin-bottom:0.9em' }, [
					E('label', { 'style': 'display:inline-block; min-width:14em' }, _('提交状态')),
					submitStatus
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
				generateButton,
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
