const DEFAULT_WS_URL = 'ws://localhost:8080/ws/ui';
const STREAMS = ['relay_status', 'cube_update', 'fleet_state', 'log'];
const SOCKET_STATE = { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 };
const MAX_LOGS = 200;
const DEFAULT_FIELD_BOUNDS = {
  top_left: { x: 45, y: 45 },
  bottom_right: { x: 455, y: 455 },
};

class ControlState extends EventTarget {
  constructor(initialUrl = DEFAULT_WS_URL) {
    super();
    this.connection = { status: 'disconnected', url: initialUrl };
    this.relays = [];
    this.cubes = new Map();
    this.logs = [];
    this.fleetState = null;
    this.selected = new Set();
    this.lastActivity = 'Idle';
    this.field = cloneFieldBounds(DEFAULT_FIELD_BOUNDS);
    this.emitAll();
  }

  emitAll() {
    this.emit('connection', this.connection);
    this.emit('relays', this.relays);
    this.emit('cubes', this.getCubes());
    this.emit('logs', this.logs);
    this.emit('selection', this.getSelection());
    this.emit('fleet', this.fleetState);
    this.emit('activity', this.lastActivity);
    this.emit('field', this.getField());
  }

  emit(type, detail) {
    this.dispatchEvent(new CustomEvent(type, { detail }));
  }

  setConnection(status, extra = {}) {
    this.connection = { ...this.connection, status, ...extra };
    this.emit('connection', this.connection);
  }

  noteActivity(text) {
    this.lastActivity = text;
    this.emit('activity', this.lastActivity);
  }

  setRelays(relays) {
    this.relays = [...relays].map((relay) => ({ ...relay }));
    this.relays.sort((a, b) => a.relay_id.localeCompare(b.relay_id));
    this.emit('relays', this.relays);
  }

  applyRelayStatus(update = {}) {
    if (!update.relay_id) return;
    const idx = this.relays.findIndex((relay) => relay.relay_id === update.relay_id);
    if (idx === -1) {
      this.relays.push({ relay_id: update.relay_id, status: 'unknown', ...update });
    } else {
      this.relays[idx] = { ...this.relays[idx], ...update };
    }
    this.relays.sort((a, b) => a.relay_id.localeCompare(b.relay_id));
    this.emit('relays', this.relays);
  }

  setCubes(cubes) {
    this.cubes = new Map();
    cubes.forEach((cube) => {
      if (cube?.cube_id) {
        this.cubes.set(cube.cube_id, { ...cube });
      }
    });
    this.syncSelection();
    this.emit('cubes', this.getCubes());
  }

  getCubes() {
    return Array.from(this.cubes.values()).sort((a, b) => a.cube_id.localeCompare(b.cube_id));
  }

  getCube(cubeId) {
    const cube = this.cubes.get(cubeId);
    if (!cube) return null;
    return JSON.parse(JSON.stringify(cube));
  }

  applyCubeUpdate(payload = {}) {
    const updates = Array.isArray(payload.updates) ? payload.updates : [];
    let changed = false;
    updates.forEach((update) => {
      if (!update?.cube_id) return;
      const current = this.cubes.get(update.cube_id) || { cube_id: update.cube_id };
      const merged = {
        ...current,
        ...update,
        position: { ...(current.position || {}), ...(update.position || {}) },
      };
      this.cubes.set(update.cube_id, merged);
      changed = true;
    });
    if (changed) {
      this.emit('cubes', this.getCubes());
      this.syncSelection();
    }
  }

  setFleetState(data) {
    this.fleetState = data ? { ...data } : null;
    this.emit('fleet', this.fleetState);
  }

  getField() {
    return cloneFieldBounds(this.field);
  }

  setField(bounds) {
    const normalized = normalizeFieldBounds(bounds);
    if (!normalized) return;
    if (fieldsEqual(normalized, this.field)) return;
    this.field = normalized;
    this.emit('field', this.getField());
  }

  addLog(entry = {}) {
    const supportsRandomUUID = typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function';
    const normalized = {
      id: supportsRandomUUID ? crypto.randomUUID() : `${Date.now()}-${Math.random()}`,
      level: entry.level || 'info',
      message: entry.message || '',
      context: entry.context || null,
      timestamp: entry.timestamp ?? Date.now(),
    };
    this.logs.unshift(normalized);
    if (this.logs.length > MAX_LOGS) {
      this.logs.length = MAX_LOGS;
    }
    this.emit('logs', this.logs.slice());
  }

  clearLogs() {
    this.logs = [];
    this.emit('logs', this.logs);
  }

  toggleCubeSelection(cubeId) {
    if (!cubeId) return this.getSelection();
    if (this.selected.has(cubeId)) {
      this.selected.delete(cubeId);
    } else {
      this.selected.add(cubeId);
    }
    const selection = this.getSelection();
    this.emit('selection', selection);
    return selection;
  }

  clearSelection() {
    if (this.selected.size === 0) return;
    this.selected.clear();
    this.emit('selection', []);
  }

  getSelection() {
    return Array.from(this.selected.values()).sort((a, b) => a.localeCompare(b));
  }

  syncSelection() {
    let mutated = false;
    for (const cubeId of Array.from(this.selected)) {
      if (!this.cubes.has(cubeId)) {
        this.selected.delete(cubeId);
        mutated = true;
      }
    }
    if (mutated) {
      this.emit('selection', this.getSelection());
    }
  }
}

class ToastManager {
  constructor(container) {
    this.container = container;
  }

  show(message, variant = 'info') {
    if (!this.container) return;
    const toast = document.createElement('div');
    toast.className = `toast ${variant}`;
    toast.innerHTML = `
      <span>${message}</span>
      <span class="timestamp">${new Date().toLocaleTimeString()}</span>
    `;
    this.container.appendChild(toast);
    setTimeout(() => toast.classList.add('visible'), 10);
    setTimeout(() => {
      toast.classList.add('hide');
      const remove = () => toast.remove();
      toast.addEventListener('transitionend', remove, { once: true });
      setTimeout(remove, 400);
    }, 4000);
  }
}

class ControlClient {
  constructor(state, toaster) {
    this.state = state;
    this.toaster = toaster;
    this.socket = null;
    this.pending = new Map();
    this.useMock = false;
    this.manualClose = false;
  }

  connect(url, { useMock = false } = {}) {
    this.disconnect();
    this.useMock = useMock;
    this.manualClose = false;
    this.state.setConnection('connecting', { url });
    this.state.noteActivity(`Connecting to ${useMock ? 'mock server' : url}`);
    try {
      this.socket = useMock ? new MockSocket() : new WebSocket(url, 'toio-ui.v1');
    } catch (error) {
      this.state.setConnection('disconnected', { url });
      this.toaster?.show(error.message, 'error');
      throw error;
    }
    this.bindSocket();
  }

  bindSocket() {
    if (!this.socket) return;
    this.socket.addEventListener('open', () => {
      this.state.setConnection('connected', { url: this.state.connection.url });
      this.toaster?.show('WebSocket connected', 'success');
      this.state.noteActivity('Connected');
      this.sendCommand('subscribe', { streams: STREAMS, include_history: true }, { silent: true });
    });

    this.socket.addEventListener('close', () => {
      if (!this.manualClose) {
        this.toaster?.show('Connection closed', 'warn');
      }
      this.state.setConnection('disconnected', { url: this.state.connection.url });
      this.state.noteActivity('Disconnected');
      this.pending.clear();
      this.socket = null;
    });

    this.socket.addEventListener('error', () => {
      this.toaster?.show('WebSocket error', 'error');
      this.state.noteActivity('Socket error');
    });

    this.socket.addEventListener('message', (event) => {
      this.handleMessage(event.data);
    });
  }

  disconnect() {
    this.manualClose = true;
    if (this.socket) {
      try {
        this.socket.close();
      } catch {
        // ignore
      }
    }
    this.socket = null;
    this.pending.clear();
    this.state.setConnection('disconnected', { url: this.state.connection.url });
  }

  isReady() {
    return this.socket && this.socket.readyState === SOCKET_STATE.OPEN;
  }

  sendCommand(type, payload = {}, options = {}) {
    if (!this.isReady()) {
      throw new Error('Connect to the control server first.');
    }
    const requestId = createRequestId(type);
    const body = {
      type,
      request_id: requestId,
      payload,
    };
    this.pending.set(requestId, { type, sentAt: Date.now(), silent: Boolean(options.silent) });
    this.socket.send(JSON.stringify(body));
    return requestId;
  }

  requestSnapshot(includeHistory = false) {
    return this.sendCommand('request_snapshot', { include_history: includeHistory });
  }

  handleMessage(raw) {
    let message;
    try {
      message = JSON.parse(raw);
    } catch (error) {
      console.error('Invalid JSON', raw);
      return;
    }
    this.state.noteActivity(`Received ${message.type || 'unknown'}`);
    switch (message.type) {
      case 'snapshot':
        if (message.payload?.field) {
          this.state.setField(message.payload.field);
        }
        this.state.setRelays(message.payload?.relays || []);
        this.state.setCubes(message.payload?.cubes || []);
        if (Array.isArray(message.payload?.history)) {
          message.payload.history.forEach((entry) => this.state.addLog(entry));
        }
        break;
      case 'field_info':
        this.state.setField(message.payload);
        break;
      case 'relay_status':
        this.state.applyRelayStatus(message.payload);
        break;
      case 'cube_update':
        this.state.applyCubeUpdate(message.payload);
        break;
      case 'fleet_state':
        this.state.setFleetState(message.payload);
        break;
      case 'log':
        this.state.addLog(message.payload || {});
        break;
      case 'ack':
        this.handleAck(message.payload, message.timestamp);
        break;
      case 'error':
        this.handleError(message.payload, message.timestamp);
        break;
      default:
        console.warn('Unhandled message type', message.type);
        break;
    }
  }

  handleAck(payload = {}, timestamp) {
    const pending = payload.request_id ? this.pending.get(payload.request_id) : null;
    if (pending) {
      this.pending.delete(payload.request_id);
    }
    const silent = pending?.silent;
    if (!silent) {
      const label = pending?.type || 'request';
      this.toaster?.show(`${label} succeeded`, 'success');
    }
    this.state.addLog({
      level: 'info',
      message: `ACK ${payload.request_id || ''}`,
      timestamp: timestamp || Date.now(),
    });
  }

  handleError(payload = {}, timestamp) {
    if (payload.request_id) {
      this.pending.delete(payload.request_id);
    }
    const message = payload.message || 'Request failed';
    this.toaster?.show(message, 'error');
    this.state.addLog({
      level: 'error',
      message: `${payload.code || 'error'}: ${message}`,
      timestamp: timestamp || Date.now(),
    });
  }
}

class MockSocket extends EventTarget {
  constructor() {
    super();
    this.readyState = SOCKET_STATE.CONNECTING;
    this.ledPalette = [
      { r: 255, g: 85, b: 0 },
      { r: 0, g: 180, b: 255 },
      { r: 120, g: 255, b: 120 },
      { r: 255, g: 120, b: 180 },
      { r: 220, g: 200, b: 50 },
    ];
    this.relays = [
      { relay_id: 'relay-a', status: 'connected', rtt_ms: 32 },
      { relay_id: 'relay-b', status: 'connected', rtt_ms: 48 },
      { relay_id: 'relay-c', status: 'connecting', rtt_ms: null },
    ];
    this.cubes = new Map();
    this.fieldBounds = cloneFieldBounds(DEFAULT_FIELD_BOUNDS);
    '38t j2T d2R 534 h9Q p0L q1X r2Y s3Z t4A'.split(' ').forEach((id, index) => {
      this.cubes.set(id, {
        cube_id: id,
        relay_id: this.relays[index % this.relays.length].relay_id,
        position: { x: 60 + index * 30, y: 60 + ((index % 3) * 60), deg: 0, on_mat: true },
        battery: 60 + (index * 3) % 40,
        state: 'idle',
        led: this.sampleLedColor(index),
      });
    });
    setTimeout(() => {
      this.readyState = SOCKET_STATE.OPEN;
      this.dispatchEvent(new Event('open'));
      this.startLoops();
    }, 400);
  }

  startLoops() {
    this.cubeTimer = setInterval(() => {
      const updates = [];
      for (const cube of this.cubes.values()) {
        const next = {
          ...cube,
          position: {
            x: clamp((cube.position?.x || 0) + randomBetween(-15, 15), 0, 450),
            y: clamp((cube.position?.y || 0) + randomBetween(-15, 15), 0, 450),
            deg: ((cube.position?.deg || 0) + randomBetween(-15, 15) + 360) % 360,
            on_mat: true,
          },
          state: Math.random() > 0.6 ? 'moving' : 'idle',
          led: Math.random() > 0.92 ? this.sampleLedColor(randomBetween(0, 4)) : cube.led,
        };
        this.cubes.set(cube.cube_id, next);
        updates.push(next);
      }
      this.push({
        type: 'cube_update',
        timestamp: Date.now(),
        payload: { updates },
      });
    }, 1200);

    this.relayTimer = setInterval(() => {
      const relay = this.relays[Math.floor(Math.random() * this.relays.length)];
      relay.status = Math.random() > 0.1 ? 'connected' : 'connecting';
      relay.rtt_ms = relay.status === 'connected' ? randomBetween(25, 80) : null;
      this.push({
        type: 'relay_status',
        timestamp: Date.now(),
        payload: { ...relay, message: relay.status === 'connected' ? 'OK' : 'Reconnecting' },
      });
    }, 3500);

    this.logTimer = setInterval(() => {
      this.push({
        type: 'log',
        timestamp: Date.now(),
        payload: {
          level: Math.random() > 0.8 ? 'warn' : 'info',
          message: 'Mock server heartbeat',
        },
      });
    }, 5000);

    this.fleetTimer = setInterval(() => {
      this.push({
        type: 'fleet_state',
        timestamp: Date.now(),
        payload: {
          tick_hz: 30,
          tasks_in_queue: randomBetween(0, 5),
          warnings: Math.random() > 0.7 ? ['Mock: cube stalled'] : [],
        },
      });
    }, 4000);
  }

  send(raw) {
    const message = JSON.parse(raw);
    const { type, request_id } = message;
    switch (type) {
      case 'subscribe':
        this.pushAck(request_id);
        this.pushFieldInfo();
        this.sendSnapshot();
        break;
      case 'manual_drive':
      case 'set_goal':
      case 'set_led':
      case 'set_group':
      case 'request_snapshot':
        this.pushAck(request_id);
        if (type === 'request_snapshot') {
          this.sendSnapshot();
        }
        break;
      default:
        this.push({
          type: 'error',
          timestamp: Date.now(),
          payload: { request_id, code: 'unknown_type', message: `Unknown command ${type}` },
        });
        break;
    }
  }

  sendSnapshot() {
    this.push({
      type: 'snapshot',
      timestamp: Date.now(),
      payload: {
        field: this.fieldBounds,
        relays: this.relays,
        cubes: Array.from(this.cubes.values()),
        history: [],
      },
    });
  }

  pushFieldInfo() {
    this.push({
      type: 'field_info',
      timestamp: Date.now(),
      payload: this.fieldBounds,
    });
  }

  pushAck(requestId) {
    this.push({
      type: 'ack',
      timestamp: Date.now(),
      payload: { request_id: requestId },
    });
  }

  push(message) {
    if (this.readyState !== SOCKET_STATE.OPEN) return;
    const data = JSON.stringify(message);
    let event;
    if (typeof MessageEvent === 'function') {
      event = new MessageEvent('message', { data });
    } else {
      event = new Event('message');
      event.data = data;
    }
    this.dispatchEvent(event);
  }

  close() {
    this.readyState = SOCKET_STATE.CLOSED;
    clearInterval(this.cubeTimer);
    clearInterval(this.relayTimer);
    clearInterval(this.logTimer);
    clearInterval(this.fleetTimer);
    this.dispatchEvent(new Event('close'));
  }

  sampleLedColor(seed = 0) {
    const color = this.ledPalette[seed % this.ledPalette.length] || this.ledPalette[0];
    return { ...color };
  }
}

class FieldCanvas {
  constructor(canvas, state) {
    this.canvas = canvas;
    this.state = state;
    this.ctx = canvas.getContext('2d');
    this.size = { width: canvas.clientWidth, height: canvas.clientHeight };
    this.dpr = window.devicePixelRatio || 1;
    this.padding = 32;
    this.bounds = state.getField ? state.getField() : cloneFieldBounds(DEFAULT_FIELD_BOUNDS);
    this.view = { scale: 1, offsetX: 0, offsetY: 0, minScale: 0.6, maxScale: 3 };
    this.needsRender = true;
    this.isPointerDown = false;
    this.lastPointer = null;

    state.addEventListener('cubes', () => this.queueRender());
    state.addEventListener('selection', () => this.queueRender());

    this.resizeHandler = () => this.handleResize();
    if (typeof ResizeObserver !== 'undefined') {
      this.resizeObserver = new ResizeObserver(this.resizeHandler);
      this.resizeObserver.observe(canvas);
    } else {
      window.addEventListener('resize', this.resizeHandler);
    }

    this.onPointerMove = (event) => this.handlePointerMove(event);
    this.onPointerUp = (event) => this.handlePointerUp(event);

    this.attachInteractions();
    this.handleResize();
    this.renderLoop();
  }

  attachInteractions() {
    this.canvas.addEventListener(
      'wheel',
      (event) => {
        this.handleWheel(event);
      },
      { passive: false },
    );
    this.canvas.addEventListener('pointerdown', (event) => this.handlePointerDown(event));
    this.canvas.addEventListener('dblclick', () => this.resetView());
  }

  updateBounds(bounds) {
    if (!bounds) return;
    this.bounds = cloneFieldBounds(bounds);
    this.resetView();
  }

  handleWheel(event) {
    event.preventDefault();
    const zoomDelta = event.deltaY > 0 ? 0.9 : 1.1;
    const nextScale = clamp(this.view.scale * zoomDelta, this.view.minScale, this.view.maxScale);
    if (nextScale === this.view.scale) return;

    const mouseX = event.offsetX;
    const mouseY = event.offsetY;
    const worldX = (mouseX - this.view.offsetX) / this.view.scale;
    const worldY = (mouseY - this.view.offsetY) / this.view.scale;

    this.view.scale = nextScale;
    this.view.offsetX = mouseX - worldX * this.view.scale;
    this.view.offsetY = mouseY - worldY * this.view.scale;
    this.queueRender();
  }

  handlePointerDown(event) {
    this.isPointerDown = true;
    this.lastPointer = { x: event.clientX, y: event.clientY };
    this.canvas.setPointerCapture(event.pointerId);
    this.canvas.classList.add('is-panning');
    this.canvas.addEventListener('pointermove', this.onPointerMove);
    this.canvas.addEventListener('pointerup', this.onPointerUp);
    this.canvas.addEventListener('pointercancel', this.onPointerUp);
    this.canvas.addEventListener('pointerleave', this.onPointerUp);
  }

  handlePointerMove(event) {
    if (!this.isPointerDown || !this.lastPointer) return;
    const dx = event.clientX - this.lastPointer.x;
    const dy = event.clientY - this.lastPointer.y;
    this.lastPointer = { x: event.clientX, y: event.clientY };
    this.view.offsetX += dx;
    this.view.offsetY += dy;
    this.queueRender();
  }

  handlePointerUp(event) {
    if (event.type === 'pointerup' || event.type === 'pointercancel' || event.type === 'pointerleave') {
      this.isPointerDown = false;
      this.lastPointer = null;
      this.canvas.classList.remove('is-panning');
      if (this.canvas.hasPointerCapture?.(event.pointerId)) {
        this.canvas.releasePointerCapture(event.pointerId);
      }
      this.canvas.removeEventListener('pointermove', this.onPointerMove);
      this.canvas.removeEventListener('pointerup', this.onPointerUp);
      this.canvas.removeEventListener('pointercancel', this.onPointerUp);
      this.canvas.removeEventListener('pointerleave', this.onPointerUp);
    }
  }

  resetView() {
    this.view.scale = 1;
    this.view.offsetX = 0;
    this.view.offsetY = 0;
    this.queueRender();
  }

  handleResize() {
    const width = this.canvas.clientWidth;
    const height = this.canvas.clientHeight;
    if (!width || !height) return;
    this.size = { width, height };
    this.dpr = window.devicePixelRatio || 1;
    const displayWidth = Math.round(width * this.dpr);
    const displayHeight = Math.round(height * this.dpr);
    if (this.canvas.width !== displayWidth || this.canvas.height !== displayHeight) {
      this.canvas.width = displayWidth;
      this.canvas.height = displayHeight;
    }
    this.queueRender();
  }

  queueRender() {
    this.needsRender = true;
  }

  renderLoop() {
    if (this.needsRender) {
      this.draw();
      this.needsRender = false;
    }
    requestAnimationFrame(() => this.renderLoop());
  }

  draw() {
    const ctx = this.ctx;
    const { width, height } = this.size;
    if (!width || !height) return;
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.setTransform(
      this.view.scale * this.dpr,
      0,
      0,
      this.view.scale * this.dpr,
      this.view.offsetX * this.dpr,
      this.view.offsetY * this.dpr,
    );

    this.drawGrid(ctx, width, height);
    const cubes = this.state.getCubes();
    const selection = new Set(this.state.getSelection());
    cubes.forEach((cube) => {
      if (!cube.position) return;
      const projected = this.projectPosition(cube.position, width, height);
      if (!projected) return;
      const heading = cube.position.deg ?? cube.position.angle ?? 0;
      const selected = selection.has(cube.cube_id);
      const ledColor = getLedColorFromCube(cube) || { r: 255, g: 255, b: 255 };
      this.drawCube(ctx, projected.x, projected.y, heading, cube.cube_id, selected, ledColor, width, height);
    });
  }

  drawGrid(ctx, width, height) {
    const fieldWidth = Math.max(1, this.bounds.bottom_right.x - this.bounds.top_left.x);
    const fieldHeight = Math.max(1, this.bounds.bottom_right.y - this.bounds.top_left.y);
    const usableWidth = width - this.padding * 2;
    const usableHeight = height - this.padding * 2;

    ctx.save();
    ctx.fillStyle = 'rgba(0, 0, 0, 0.03)';
    ctx.fillRect(-width, -height, width * 3, height * 3);
    ctx.fillStyle = '#f5f7fb';
    ctx.fillRect(this.padding, this.padding, usableWidth, usableHeight);

    ctx.strokeStyle = 'rgba(60, 70, 90, 0.12)';
    ctx.lineWidth = 1;
    ctx.strokeRect(this.padding, this.padding, usableWidth, usableHeight);

    ctx.setLineDash([4, 6]);
    ctx.strokeStyle = 'rgba(0,0,0,0.08)';
    const gridStep = 50;
    for (let mm = gridStep; mm < fieldWidth; mm += gridStep) {
      const x = this.padding + (mm / fieldWidth) * usableWidth;
      ctx.beginPath();
      ctx.moveTo(x, this.padding);
      ctx.lineTo(x, this.padding + usableHeight);
      ctx.stroke();
    }
    for (let mm = gridStep; mm < fieldHeight; mm += gridStep) {
      const y = this.padding + (mm / fieldHeight) * usableHeight;
      ctx.beginPath();
      ctx.moveTo(this.padding, y);
      ctx.lineTo(this.padding + usableWidth, y);
      ctx.stroke();
    }
    ctx.restore();
  }

  projectPosition(position, width, height) {
    if (typeof position.x !== 'number' || typeof position.y !== 'number') return null;
    const topLeft = this.bounds.top_left;
    const bottomRight = this.bounds.bottom_right;
    const mmWidth = Math.max(1, bottomRight.x - topLeft.x);
    const mmHeight = Math.max(1, bottomRight.y - topLeft.y);
    const usableWidth = width - this.padding * 2;
    const usableHeight = height - this.padding * 2;
    const clampedX = clamp(position.x, topLeft.x, bottomRight.x);
    const clampedY = clamp(position.y, topLeft.y, bottomRight.y);
    const xNorm = (clampedX - topLeft.x) / mmWidth;
    const yNorm = (clampedY - topLeft.y) / mmHeight;
    return {
      x: this.padding + xNorm * usableWidth,
      y: this.padding + yNorm * usableHeight,
    };
  }

  drawCube(ctx, x, y, heading, label, selected, ledColor, width, height) {
    ctx.save();
    ctx.translate(x, y);
    ctx.rotate((heading * Math.PI) / 180);
    const baseSize = Math.max(10, Math.min(20, (width + height) / 90));
    const size = selected ? baseSize + 4 : baseSize;
    ctx.lineWidth = selected ? 2.5 : 1.5;
    const fillColor = `rgba(${ledColor.r}, ${ledColor.g}, ${ledColor.b}, ${selected ? 0.95 : 0.8})`;
    const strokeColor = selected ? '#ffffff' : 'rgba(20,20,20,0.8)';
    ctx.fillStyle = fillColor;
    ctx.strokeStyle = strokeColor;
    ctx.shadowColor = selected ? `rgba(${ledColor.r}, ${ledColor.g}, ${ledColor.b}, 0.8)` : 'transparent';
    ctx.shadowBlur = selected ? 18 : 0;
    ctx.beginPath();
    ctx.rect(-size / 2, -size / 2, size, size);
    ctx.stroke();
    ctx.fill();
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(size / 2, 0);
    ctx.stroke();
    ctx.restore();

    ctx.save();
    ctx.fillStyle = '#ffffff';
    ctx.font = '12px "Inter", sans-serif';
    ctx.fillText(label, x + 8, y - 8);
    ctx.restore();
  }

}

function initApp() {
  const refs = {
    connectionDot: document.getElementById('connection-dot'),
    connectionLabel: document.getElementById('connection-label'),
    connectBtn: document.getElementById('connect-btn'),
    wsUrl: document.getElementById('ws-url'),
    mockToggle: document.getElementById('mock-toggle'),
    snapshotBtn: document.getElementById('snapshot-btn'),
    lastActivity: document.getElementById('last-activity'),
    fleetSummary: document.getElementById('fleet-summary'),
    relaySummary: document.getElementById('relay-summary'),
    relayList: document.getElementById('relay-list'),
    selectedCount: document.getElementById('selected-count'),
    clearSelectionBtn: document.getElementById('clear-selection-btn'),
    resetViewBtn: document.getElementById('reset-view-btn'),
    cubeTable: document.getElementById('cube-table'),
    cubeSummary: document.getElementById('cube-summary'),
    cubeFilter: document.getElementById('cube-filter'),
    manualDriveForm: document.getElementById('manual-drive-form'),
    goalForm: document.getElementById('goal-form'),
    ledForm: document.getElementById('led-form'),
    groupForm: document.getElementById('group-form'),
    logList: document.getElementById('log-list'),
    clearLogBtn: document.getElementById('clear-log-btn'),
    toastContainer: document.getElementById('toast-container'),
    canvas: document.getElementById('field-canvas'),
    fieldExtentLabel: document.getElementById('field-extent-label'),
    ledColorPreview: document.getElementById('led-color-preview'),
  };
  refs.canvasWrapper = refs.canvas?.closest('.canvas-wrapper') || null;

  refs.wsUrl.value ||= DEFAULT_WS_URL;

  const state = new ControlState(refs.wsUrl.value);
  const toaster = new ToastManager(refs.toastContainer);
  const client = new ControlClient(state, toaster);
  const fieldCanvas = new FieldCanvas(refs.canvas, state);
  const { schedule: triggerLedAutoSend, flush: flushLedAutoSend } = createLedAutoSender();
  const ledColorControls = setupLedColorControls(triggerLedAutoSend);
  const ledBrightnessInput = refs.ledForm?.elements?.brightness;
  if (ledBrightnessInput) {
    ledBrightnessInput.addEventListener('input', triggerLedAutoSend);
  }

  let cubeFilter = '';

  state.addEventListener('connection', (event) => renderConnection(event.detail));
  state.addEventListener('activity', (event) => renderActivity(event.detail));
  state.addEventListener('relays', (event) => renderRelays(event.detail));
  state.addEventListener('cubes', () => {
    renderCubes();
    syncLedFormWithSelection(state.getSelection());
  });
  state.addEventListener('selection', (event) => {
    renderSelection(event.detail);
    syncLedFormWithSelection(event.detail);
  });
  state.addEventListener('logs', (event) => renderLogs(event.detail));
  state.addEventListener('fleet', (event) => renderFleet(event.detail));
  state.addEventListener('field', (event) => {
    renderFieldMeta(event.detail);
    fieldCanvas.updateBounds(event.detail);
  });

  refs.connectBtn.addEventListener('click', () => {
    const status = state.connection.status;
    if (status === 'connected' || status === 'connecting') {
      client.disconnect();
      return;
    }
    try {
      client.connect(refs.wsUrl.value.trim() || DEFAULT_WS_URL, {
        useMock: refs.mockToggle.checked,
      });
    } catch (error) {
      toaster.show(error.message, 'error');
    }
  });

  refs.snapshotBtn.addEventListener('click', () => {
    try {
      client.requestSnapshot();
    } catch (error) {
      toaster.show(error.message, 'error');
    }
  });

  refs.clearSelectionBtn.addEventListener('click', () => state.clearSelection());
  if (refs.resetViewBtn) {
    refs.resetViewBtn.addEventListener('click', () => fieldCanvas.resetView());
  }

  refs.cubeFilter.addEventListener('input', (event) => {
    cubeFilter = event.target.value.trim().toLowerCase();
    renderCubes();
  });

  refs.clearLogBtn.addEventListener('click', () => state.clearLogs());

  handleForm(refs.manualDriveForm, (form) => {
    const targets = resolveTargets(form.elements.targets.value);
    const left = clamp(requireNumber(form.elements.left.value, 'Left speed'), -100, 100);
    const right = clamp(requireNumber(form.elements.right.value, 'Right speed'), -100, 100);
    const payload = {
      targets,
      left,
      right,
    };
    client.sendCommand('manual_drive', payload);
  });

  handleForm(refs.goalForm, (form) => {
    const targets = resolveTargets(form.elements.targets.value);
    const goal = {
      x: requireNumber(form.elements.x.value, 'X'),
      y: requireNumber(form.elements.y.value, 'Y'),
    };
    if (form.elements.angle.value) {
      goal.angle = Number(form.elements.angle.value);
    }
    const payload = {
      targets,
      goal,
    };
    if (form.elements.priority.value) {
      payload.priority = Number(form.elements.priority.value);
    }
    if (form.elements.keep_history.checked) {
      payload.keep_history = true;
    }
    client.sendCommand('set_goal', payload);
  });

  handleForm(refs.ledForm, () => {
    flushLedAutoSend();
    sendLedCommand();
  });

  handleForm(refs.groupForm, (form) => {
    const groupId = form.elements.group_id.value.trim();
    if (!groupId) {
      throw new Error('Group ID is required.');
    }
    const members = resolveTargets(form.elements.members.value, { allowEmptySelection: false });
    client.sendCommand('set_group', { group_id: groupId, members });
  });

  function handleForm(form, handler) {
    form.addEventListener('submit', (event) => {
      event.preventDefault();
      try {
        handler(form);
      } catch (error) {
        toaster.show(error.message || 'Failed to send command', 'error');
      }
    });
  }

  function setupLedColorControls(onChange) {
    const fallback = { r: 255, g: 85, b: 0 };
    const form = refs.ledForm;
    if (!form) {
      return { getColor: () => ({ ...fallback }), update: () => {}, setColor: () => {} };
    }
    const sliders = {
      r: form.querySelector("input[name='color_r']"),
      g: form.querySelector("input[name='color_g']"),
      b: form.querySelector("input[name='color_b']"),
    };
    const outputs = {
      r: form.querySelector("[data-color-output='r']"),
      g: form.querySelector("[data-color-output='g']"),
      b: form.querySelector("[data-color-output='b']"),
    };
    const preview = refs.ledColorPreview;

    let suppressAuto = false;
    const clampChannel = (value) => clamp(Number(value ?? 0), 0, 255);
    const getColor = () => ({
      r: clampChannel(sliders.r?.value ?? fallback.r),
      g: clampChannel(sliders.g?.value ?? fallback.g),
      b: clampChannel(sliders.b?.value ?? fallback.b),
    });

    const update = () => {
      const color = getColor();
      Object.entries(outputs).forEach(([channel, element]) => {
        if (element) {
          element.textContent = String(color[channel]);
        }
      });
      if (preview) {
        preview.textContent = `rgb(${color.r}, ${color.g}, ${color.b})`;
        preview.style.background = `rgba(${color.r}, ${color.g}, ${color.b}, 0.25)`;
        preview.style.borderColor = `rgba(${color.r}, ${color.g}, ${color.b}, 0.6)`;
      }
    };

    Object.values(sliders).forEach((input) => {
      if (!input) return;
      input.addEventListener('input', () => {
        update();
        if (!suppressAuto) {
          onChange?.();
        }
      });
    });
    update();
    const setColor = (color) => {
      if (!color) return;
      suppressAuto = true;
      if (sliders.r) sliders.r.value = clampChannel(color.r);
      if (sliders.g) sliders.g.value = clampChannel(color.g);
      if (sliders.b) sliders.b.value = clampChannel(color.b);
      update();
      suppressAuto = false;
    };
    return { getColor, update, setColor };
  }

  function sendLedCommand({ silent = false, allowEmptyTargets = false } = {}) {
    if (!refs.ledForm) return;
    const form = refs.ledForm;
    const targets = resolveTargets(form.elements.targets.value, { allowEmptySelection: allowEmptyTargets });
    if (!targets.length && allowEmptyTargets) {
      return;
    }
    const color = ledColorControls.getColor();
    const brightness = Number(form.elements.brightness.value);
    let computedColor = color;
    if (!Number.isNaN(brightness) && brightness >= 0 && brightness <= 255) {
      const scale = brightness / 255;
      computedColor = {
        r: Math.round(color.r * scale),
        g: Math.round(color.g * scale),
        b: Math.round(color.b * scale),
      };
    }
    const payload = { targets, color: computedColor };
    client.sendCommand('set_led', payload, { silent });
  }

  function createLedAutoSender() {
    let debounceTimer = null;
    let throttleTimer = null;
    const INTERVAL_MS = 50;

    const sendAuto = () => {
      try {
        sendLedCommand({ silent: true, allowEmptyTargets: true });
      } catch (error) {
        console.debug('LED auto-send skipped:', error.message);
      }
    };

    const schedule = () => {
      if (!refs.ledForm) return;
      if (!throttleTimer) {
        throttleTimer = setTimeout(() => {
          throttleTimer = null;
        }, INTERVAL_MS);
        sendAuto();
      }
      clearTimeout(debounceTimer);
      debounceTimer = setTimeout(() => {
        throttleTimer = null;
        sendAuto();
      }, INTERVAL_MS);
    };

    const flush = () => {
      clearTimeout(debounceTimer);
      clearTimeout(throttleTimer);
      throttleTimer = null;
      debounceTimer = null;
      sendAuto();
    };

    return { schedule, flush };
  }

  function resolveTargets(raw, options = {}) {
    const typed = splitIds(raw);
    if (typed.length) return typed;
    const selection = state.getSelection();
    if (selection.length) return selection;
    if (options.allowEmptySelection) return [];
    throw new Error('Select at least one cube or enter IDs.');
  }

  function renderConnection(connection) {
    const { status, url } = connection;
    const label = status.charAt(0).toUpperCase() + status.slice(1);
    refs.connectionLabel.textContent = `${label} ${url ? `@ ${url}` : ''}`;
    refs.connectionDot.className = `status-dot ${status === 'connected' ? 'is-online' : status === 'connecting' ? 'is-connecting' : 'is-offline'}`;
    const buttonLabel =
      status === 'connected' ? 'Disconnect' : status === 'connecting' ? 'Cancel' : 'Connect';
    refs.connectBtn.textContent = buttonLabel;
    const disableInputs = status === 'connecting' || status === 'connected';
    refs.wsUrl.disabled = disableInputs;
    refs.mockToggle.disabled = disableInputs;
    refs.snapshotBtn.disabled = status !== 'connected';
  }

  function renderActivity(text) {
    refs.lastActivity.textContent = `Last activity: ${text}`;
  }

  function renderRelays(relays) {
    if (!relays.length) {
      refs.relayList.innerHTML = `<p class="placeholder">No relay data yet.</p>`;
      refs.relaySummary.textContent = '0 online';
      return;
    }
    const online = relays.filter((relay) => relay.status === 'connected').length;
    refs.relaySummary.textContent = `${online}/${relays.length} connected`;
    refs.relayList.innerHTML = relays
      .map(
        (relay) => `
        <article class="relay-card">
          <header>
            <span>${relay.relay_id}</span>
            <span class="status ${relay.status}">${relay.status}</span>
          </header>
          <div class="muted">
            RTT: ${relay.rtt_ms != null ? `${relay.rtt_ms} ms` : '—'}
          </div>
          <div class="muted">${relay.message || ''}</div>
        </article>
      `,
      )
      .join('');
  }

  function renderCubes() {
    const cubes = state.getCubes();
    refs.cubeSummary.textContent = `${cubes.length} cube(s)`;
    const body = refs.cubeTable.querySelector('tbody');
    if (!cubes.length) {
      body.innerHTML = `<tr><td colspan="6" class="placeholder">No cubes yet.</td></tr>`;
      return;
    }
    const filtered = cubes.filter((cube) => {
      if (!cubeFilter) return true;
      const comparable = `${cube.cube_id} ${cube.relay_id || ''} ${cube.state || ''} ${cube.goal_id || ''}`.toLowerCase();
      return comparable.includes(cubeFilter);
    });
    if (!filtered.length) {
      body.innerHTML = `<tr><td colspan="6" class="placeholder">No cubes match this filter.</td></tr>`;
      return;
    }
    const selection = new Set(state.getSelection());
    body.innerHTML = filtered
      .map((cube) => {
        const position = cube.position
          ? `${Math.round(cube.position.x ?? 0)}, ${Math.round(cube.position.y ?? 0)} (${Math.round(cube.position.deg ?? 0)}°)`
          : '—';
        const battery = cube.battery != null ? `${cube.battery}%` : cube.battery === 0 ? '0%' : '—';
        const ledInfo = formatLedCell(cube);
        return `
          <tr data-cube-id="${cube.cube_id}" class="${selection.has(cube.cube_id) ? 'is-selected' : ''}">
            <td>${cube.cube_id}</td>
            <td>${cube.relay_id || '—'}</td>
            <td>${battery}</td>
            <td>${ledInfo}</td>
            <td>${cube.state || 'unknown'}</td>
            <td>${position}</td>
          </tr>
        `;
      })
      .join('');

    body.querySelectorAll('tr').forEach((row) => {
      const cubeId = row.dataset.cubeId;
      if (!cubeId) return;
      row.addEventListener('click', () => {
        state.toggleCubeSelection(cubeId);
      });
    });
  }

  function renderSelection(list) {
    refs.selectedCount.textContent = `${list.length} cube(s) selected${list.length ? `: ${list.join(', ')}` : ''}`;
  }

  function renderLogs(logs) {
    if (!logs.length) {
      refs.logList.innerHTML = `<li class="placeholder">No log entries yet.</li>`;
      return;
    }
    refs.logList.innerHTML = logs
      .slice(0, 40)
      .map(
        (log) => `
        <li class="log-item">
          <div>
            <div class="level ${log.level || 'info'}">${(log.level || 'info').toUpperCase()}</div>
            <div>${log.message}</div>
          </div>
          <div class="timestamp">${formatTime(log.timestamp)}</div>
        </li>
      `,
      )
      .join('');
  }

  function renderFleet(fleet) {
    if (!fleet) {
      refs.fleetSummary.textContent = 'Fleet: -';
      return;
    }
    const warnings = Array.isArray(fleet.warnings) && fleet.warnings.length ? ` / warnings: ${fleet.warnings.length}` : '';
    refs.fleetSummary.textContent = `Fleet: ${fleet.tick_hz || 0}Hz / tasks: ${fleet.tasks_in_queue || 0}${warnings}`;
  }

  function renderFieldMeta(bounds) {
    const normalized = normalizeFieldBounds(bounds);
    if (refs.fieldExtentLabel) {
      const topLeft = normalized.top_left;
      const bottomRight = normalized.bottom_right;
      const width = (bottomRight.x - topLeft.x).toFixed(0);
      const height = (bottomRight.y - topLeft.y).toFixed(0);
      refs.fieldExtentLabel.textContent = `Field: (${topLeft.x}, ${topLeft.y}) → (${bottomRight.x}, ${bottomRight.y}) mm / ${width}×${height}mm`;
    }
    if (refs.canvasWrapper) {
      const fieldWidth = normalized.bottom_right.x - normalized.top_left.x;
      const fieldHeight = normalized.bottom_right.y - normalized.top_left.y;
      const ratio = fieldWidth / fieldHeight;
      if (Number.isFinite(ratio) && ratio > 0) {
        refs.canvasWrapper.style.aspectRatio = `${fieldWidth} / ${fieldHeight}`;
      }
    }
  }

  function syncLedFormWithSelection(selection) {
    if (!refs.ledForm || typeof ledColorControls.setColor !== 'function') return;
    if (!Array.isArray(selection)) selection = [];
    if (selection.length !== 1) {
      refs.ledForm.classList.remove('is-linked');
      return;
    }
    const cube = state.getCube(selection[0]);
    if (!cube?.led) {
      refs.ledForm.classList.remove('is-linked');
      return;
    }
    ledColorControls.setColor(cube.led);
    refs.ledForm.classList.add('is-linked');
  }

  renderConnection(state.connection);
  renderActivity(state.lastActivity);
  renderRelays(state.relays);
  renderCubes();
  renderLogs(state.logs);
  renderSelection(state.getSelection());
  renderFleet(state.fleetState);
  renderFieldMeta(state.getField());
  syncLedFormWithSelection(state.getSelection());
}

function clamp(value, min, max) {
  if (Number.isNaN(value)) return min;
  return Math.min(Math.max(value, min), max);
}

function getLedColorFromCube(cube) {
  const led = cube?.led;
  if (
    led &&
    Number.isFinite(led.r) &&
    Number.isFinite(led.g) &&
    Number.isFinite(led.b)
  ) {
    return {
      r: clamp(Math.round(led.r), 0, 255),
      g: clamp(Math.round(led.g), 0, 255),
      b: clamp(Math.round(led.b), 0, 255),
    };
  }
  return null;
}

function ledToCss(color, alpha = 1) {
  return `rgba(${color.r}, ${color.g}, ${color.b}, ${alpha})`;
}

function formatLedCell(cube) {
  const led = getLedColorFromCube(cube);
  if (!led) return '—';
  const cssColor = ledToCss(led);
  return `
    <span class="led-indicator">
      <span class="led-swatch" style="background:${cssColor};"></span>
      <span>${led.r},${led.g},${led.b}</span>
    </span>
  `;
}

function splitIds(text = '') {
  return text
    .split(/[\s,]+/)
    .map((token) => token.trim())
    .filter(Boolean);
}

function formatTime(timestamp) {
  const date = new Date(timestamp || Date.now());
  return date.toLocaleTimeString();
}

function randomBetween(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}

function requireNumber(value, label) {
  const num = Number(value);
  if (Number.isNaN(num)) {
    throw new Error(`${label} is required.`);
  }
  return num;
}

function createRequestId(prefix) {
  const hasCrypto = typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function';
  if (hasCrypto) {
    return `${prefix}-${crypto.randomUUID()}`;
  }
  return `${prefix}-${Date.now()}-${Math.floor(Math.random() * 1000)}`;
}

function numberOr(value, fallback) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function cloneFieldBounds(bounds = DEFAULT_FIELD_BOUNDS) {
  const source = bounds || DEFAULT_FIELD_BOUNDS;
  return {
    top_left: {
      x: numberOr(source.top_left?.x, DEFAULT_FIELD_BOUNDS.top_left.x),
      y: numberOr(source.top_left?.y, DEFAULT_FIELD_BOUNDS.top_left.y),
    },
    bottom_right: {
      x: numberOr(source.bottom_right?.x, DEFAULT_FIELD_BOUNDS.bottom_right.x),
      y: numberOr(source.bottom_right?.y, DEFAULT_FIELD_BOUNDS.bottom_right.y),
    },
  };
}

function normalizeFieldBounds(bounds) {
  const clone = cloneFieldBounds(bounds);
  if (clone.bottom_right.x <= clone.top_left.x) {
    clone.bottom_right.x = clone.top_left.x + 1;
  }
  if (clone.bottom_right.y <= clone.top_left.y) {
    clone.bottom_right.y = clone.top_left.y + 1;
  }
  return clone;
}

function fieldsEqual(a, b) {
  if (!a || !b) return false;
  return (
    a.top_left.x === b.top_left.x &&
    a.top_left.y === b.top_left.y &&
    a.bottom_right.x === b.bottom_right.x &&
    a.bottom_right.y === b.bottom_right.y
  );
}

document.addEventListener('DOMContentLoaded', initApp);
