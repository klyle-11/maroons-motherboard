// <!-- Version 2.3 -->
// Fetch SD card information
async function fetchSD() {
  try {
    const res = await fetch('/sdinfo');
    const { total, used } = await res.json();
    document.getElementById('sd-total').textContent = `Total: ${(total/1e9).toFixed(2)} GB`;
    document.getElementById('sd-used').textContent = `Used: ${(used/1e9).toFixed(2)} GB`;
    const percent = Math.round((used / total) * 100);
    document.getElementById('sd-bar').style.width = `${percent}%`;
    document.getElementById('sd-percent').textContent = `${percent}%`;
  } catch (e) {
    console.error('Failed to fetch SD info:', e);
  }
}
async function triggerSDScan() {
  try {
    const res = await fetch('/api/sd-scan', { method: 'POST' });
    const data = await res.json();
    if (res.ok) {
      alert('SD scan started. Check console for progress. Do not use the device during this process.');
      setTimeout(fetchSD, 5000);
    } else {
      alert('Failed to start SD scan');
    }
  } catch (e) {
    console.error('SD scan error:', e);
    alert('Error starting SD scan');
  }
}
// Theme management
const THEME_KEY = 'nomad_dark_mode';

function applyThemeFlag(dark) {
  try {
    dark = !!dark;
    document.body.classList.toggle('dark', dark);
    
    const root = document.documentElement.style;
    if (dark) {
      root.setProperty('--bg', '#121212');
      root.setProperty('--text', '#e9ecef');
      root.setProperty('--muted', '#adb5bd');
      root.setProperty('--card-bg', '#1e1e1e');
      root.setProperty('--card-border', '#343a40');
      root.setProperty('--header-text', '#ffffff');
    } else {
      root.setProperty('--bg', '#f8f9fa');
      root.setProperty('--text', '#212529');
      root.setProperty('--muted', '#6c757d');
      root.setProperty('--card-bg', '#ffffff');
      root.setProperty('--card-border', '#dee2e6');
      root.setProperty('--header-text', '#ffffff');
    }
  } catch (e) {
    console.warn('applyThemeFlag failed', e);
  }
}

function initTheme() {
  try {
    const saved = localStorage.getItem(THEME_KEY);
    const isDark = saved === 'true';
    applyThemeFlag(isDark);
  } catch (e) {
    applyThemeFlag(false);
  }
}

// Initialize theme
if (document.readyState === 'loading') {
  window.addEventListener('DOMContentLoaded', initTheme);
} else {
  initTheme();
}

// Sync theme across tabs
window.addEventListener('storage', (ev) => {
  if (ev.key === THEME_KEY) {
    const dark = ev.newValue === 'true';
    applyThemeFlag(dark);
  }
});

// SHA-256 helper
async function sha256Hex(str) {
  if (!window.crypto || !crypto.subtle) {
    console.warn('SubtleCrypto unavailable; sha256Hex will return raw string (less secure).');
    return str;
  }
  const enc = new TextEncoder();
  const data = enc.encode(str || '');
  const hash = await crypto.subtle.digest('SHA-256', data);
  const bytes = new Uint8Array(hash);
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
}

// Settings management
async function loadSettings() {
  console.log("loadSettings() fired");
  try {
    const res = await fetch('/settings', { cache: 'no-store' });
    const s = await res.json();
    console.log("settings from backend:", s);

    // Update UI elements
    if (s.hasOwnProperty('adminPassword')) {
      const serverPw = s.adminPassword;
      if (serverPw === null || serverPw === '' || serverPw === 'null') {
        console.log('Admin password is disabled on server');
      } else {
        console.log('Admin password is set on server');
      }
    }

    // RGB settings
    if (s.rgbMode !== undefined) {
      ledMode = s.rgbMode;
      updateModeUI(ledMode);
    }
    if (s.rgbColor !== undefined) {
      document.getElementById('led-color').value = s.rgbColor;
    }

    // WiFi settings
    if (s.wifiSSID !== undefined) {
      document.getElementById('ssid').value = s.wifiSSID;
    }
    if (s.wifiPassword !== undefined) {
      document.getElementById('wifi-password').value = s.wifiPassword;
    }

    // Brightness
    if (s.brightness !== undefined) {
      document.getElementById('brightness').value = s.brightness;
      updateBrightnessLabel(s.brightness);
    }

    // Auto-generate
    if (s.autoGenerateMedia !== undefined) {
      isAutoGenerate = s.autoGenerateMedia;
      document.getElementById('auto-generate').checked = isAutoGenerate;
    }

    // Check authentication
    if (typeof requireAdminAuth === 'function') {
      await requireAdminAuth(s);
    }

  } catch (e) {
    console.error('Failed to load settings:', e);
  }
}

async function saveSettings() {
  try {
    const wifiPassword = document.getElementById('wifi-password').value;
    
    // Validate WiFi password length
    if (wifiPassword && wifiPassword.length < 8) {
      alert('WiFi password must be at least 8 characters long for captive portal compatibility.');
      return;
    }
    
    const settings = {
      rgbMode: ledMode,
      rgbColor: document.getElementById('led-color').value,
      wifiSSID: document.getElementById('ssid').value,
      wifiPassword: wifiPassword,
      brightness: parseInt(document.getElementById('brightness').value),
      autoGenerateMedia: document.getElementById('auto-generate').checked
    };

    const res = await fetch('/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        'body': JSON.stringify(settings)
      })
    });

    if (res.ok) {
      console.log('Settings saved successfully');
    } else {
      console.error('Failed to save settings');
    }
  } catch (e) {
    console.error('Error saving settings:', e);
  }
}

// RGB LED controls
let ledMode = 'off';

function updateModeUI(mode) {
  const buttons = {
    off: document.getElementById('mode-off'),
    solid: document.getElementById('mode-solid'),
    rainbow: document.getElementById('mode-rainbow')
  };

  // Reset all buttons
  Object.values(buttons).forEach(btn => {
    if (btn) {
      btn.classList.remove('btn-primary', 'btn-success', 'btn-secondary');
      btn.classList.add('btn-secondary');
    }
  });

  // Highlight active button
  if (buttons[mode]) {
    buttons[mode].classList.remove('btn-secondary');
    if (mode === 'off') buttons[mode].classList.add('btn-secondary');
    else if (mode === 'solid') buttons[mode].classList.add('btn-primary');
    else if (mode === 'rainbow') buttons[mode].classList.add('btn-success');
  }

  ledMode = mode;
}

async function sendModeToServer(mode) {
  try {
    if (mode === 'off') {
      await fetch('/led/onoff', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: false })
      });
    } else if (mode === 'solid') {
      await fetch('/led/onoff', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: true })
      });
    } else if (mode === 'rainbow') {
      await fetch('/led/rainbow', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
      });
    }
  } catch (e) {
    console.error('Failed to send mode to server:', e);
  }
}

async function sendColorToServer(color) {
  try {
    await fetch('/led/color', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ color: color })
    });
  } catch (e) {
    console.error('Failed to send color to server:', e);
  }
}

// Admin password functions
async function updateAdminPassword() {
  const newPw = document.getElementById('new-password').value;
  const confirmPw = document.getElementById('confirm-password').value;

  if (!newPw) {
    alert('Please enter a password');
    return;
  }

  if (newPw !== confirmPw) {
    alert('Passwords do not match');
    return;
  }

  try {
    const hashedPw = await sha256Hex(newPw);
    const settingsUpdate = {
      adminPassword: hashedPw
    };

    const res = await fetch('/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        'body': JSON.stringify(settingsUpdate)
      })
    });

    if (res.ok) {
      alert('Admin password updated successfully');
      document.getElementById('new-password').value = '';
      document.getElementById('confirm-password').value = '';
      localStorage.removeItem('nomad_admin_logged_in');
      sessionStorage.setItem('nomad_force_reauth', 'true');
      await loadSettings();
    } else {
      alert('Failed to update admin password');
    }
  } catch (e) {
    console.error('Error updating admin password:', e);
    alert('Error updating admin password');
  }
}

// WiFi settings
function updateWiFiSettings() {
  const wifiPassword = document.getElementById('wifi-password').value;
  
  // Validate WiFi password length
  if (wifiPassword && wifiPassword.length < 8) {
    alert('WiFi password must be at least 8 characters long for captive portal compatibility.');
    return;
  }
  
  saveSettings();
  alert('WiFi settings updated. Changes will take effect after restart.');
}

// Brightness control
function setBrightness(val) {
  const brightnessValue = parseInt(val);

  // Wrap the local fetch so the brightness endpoint receives a URLSearchParams-encoded body
  const _origFetch = window.fetch.bind(window);
  const fetch = (url, opts = {}) => {
    if (url === '/brightness') {
      const newBody = new URLSearchParams({
        'body': JSON.stringify({ value: brightnessValue })
      });
      const newOpts = Object.assign({}, opts, { body: newBody });
      if (newOpts.headers) {
        const headers = Object.assign({}, newOpts.headers);
        delete headers['Content-Type'];
        delete headers['content-type'];
        newOpts.headers = headers;
      }
      return _origFetch(url, newOpts);
    }
    return _origFetch(url, opts);
  };
  return fetch('/brightness', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'body=' + encodeURIComponent(JSON.stringify({ value: brightnessValue }))
  }).catch(e => console.error('Failed to set brightness:', e));
}

function updateBrightnessLabel(val) {
  document.getElementById('brightness-percent').textContent = `${val}%`;
}

// Media generation
async function generateMediaJson() {
  try {
    document.getElementById('generate-msg').textContent = 'Starting full scan...';
    await fetch('/generate-media', { method: 'POST' });
    document.getElementById('generate-msg').textContent = 'Full scan initiated. Check console for progress.';
    setTimeout(() => {
      document.getElementById('generate-msg').textContent = '';
    }, 5000);
  } catch (e) {
    console.error('Failed to generate media:', e);
    document.getElementById('generate-msg').textContent = 'Failed to start scan.';
  }
}

// Auto-generate toggle
let isAutoGenerate = false;

// Console functionality
let consoleLines = [];
const maxConsoleLines = 100;

function addConsoleLog(message, type = 'info') {
  const timestamp = new Date().toLocaleTimeString();
  const line = `[${timestamp}] ${message}`;
  
  consoleLines.push({ line, type, timestamp: Date.now() });
  
  // Keep only the last maxConsoleLines
  if (consoleLines.length > maxConsoleLines) {
    consoleLines = consoleLines.slice(-maxConsoleLines);
  }
  
  updateConsoleDisplay();
}

function updateConsoleDisplay() {
  const console = document.getElementById('console-output');
  if (!console) return;

  console.innerHTML = consoleLines.map(({ line, type }) => {
    let color = '#00ff00'; // Default green
    let icon = '';

    if (type === 'error') {
      color = '#ff4444';
      icon = '❌ ';
    } else if (type === 'warning') {
      color = '#ffaa00';
      icon = '⚠️ ';
    } else if (type === 'info') {
      color = '#00aaff';
      icon = 'ℹ️ ';
    } else if (type === 'success') {
      color = '#00ff88';
      icon = '✅ ';
    } else if (type === 'system') {
      color = '#88aaff';
      icon = '🔧 ';
    }

    return `<div style="color: ${color}; margin-bottom: 2px;">${icon}${line}</div>`;
  }).join('');

  // Auto-scroll to bottom
  console.scrollTop = console.scrollHeight;
}

async function refreshConsole() {
  try {
    // Fetch scan status
    const res = await fetch('/scan-status');
    if (res.ok) {
      const data = await res.json();

      // Update status display
      document.getElementById('scan-status-text').textContent = data.status || 'Idle';
      document.getElementById('scan-mode-text').textContent = data.mode || '—';
      document.getElementById('scan-queue-depth').textContent = data.queueDepth || '0';

      // Update scanning animation
      const statusElement = document.getElementById('scan-status-text');
      if (data.status && data.status.toLowerCase().includes('scanning')) {
        statusElement.classList.add('scanning');
      } else {
        statusElement.classList.remove('scanning');
      }
    }

    // Fetch console logs from backend
    await fetchConsoleLogs();

  } catch (e) {
    const timestamp = new Date().toLocaleTimeString();
    addConsoleLog(`[${timestamp}] ❌ Failed to fetch system status: ${e.message}`, 'error');
  }
}

async function fetchConsoleLogs() {
  try {
    const res = await fetch('/console-logs');
    if (res.ok) {
      const data = await res.json();

      // Clear existing logs and add new ones
      consoleLines = [];

      if (data.logs && data.logs.length > 0) {
        data.logs.forEach(log => {
          // Convert timestamp to readable format
          const date = new Date(log.timestamp);
          const timeStr = date.toLocaleTimeString();
          const formattedMessage = log.message && log.message.startsWith('[') ?
            log.message : `[${timeStr}] ${log.message || ''}`;

          consoleLines.push({
            line: formattedMessage,
            type: log.type || 'info',
            timestamp: log.timestamp
          });
        });
      } else {
        // Show default message if no logs
        const timeStr = new Date().toLocaleTimeString();
        consoleLines.push({
          line: `[${timeStr}] System ready. Monitoring operations...`,
          type: 'info',
          timestamp: Date.now()
        });
      }

      updateConsoleDisplay();
    }
  } catch (e) {
    // Silently handle errors to avoid spam
  }
}

async function fetchSystemInfo() {
  try {
    const timestamp = new Date().toLocaleTimeString();

    // Fetch admin status for additional info
    const adminRes = await fetch('/admin-status');
    if (adminRes.ok) {
      const adminData = await adminRes.json();

      // Log connection info if users changed
      if (!window.lastUserCount || window.lastUserCount !== adminData.users) {
        const userChange = window.lastUserCount ?
          (adminData.users > window.lastUserCount ? 'connected' : 'disconnected') : 'detected';
        addConsoleLog(`[${timestamp}] 👥 User ${userChange}: ${adminData.users} active connection${adminData.users !== 1 ? 's' : ''}`, 'info');
        window.lastUserCount = adminData.users;
      }
    }

    // Fetch CPU temperature
    const tempRes = await fetch('/cpu-temp');
    if (tempRes.ok) {
      const tempData = await tempRes.json();

      // Log temperature warnings
      if (tempData.temp > 70) {
        addConsoleLog(`[${timestamp}] 🌡️ High CPU temperature detected: ${tempData.temp}°C`, 'warning');
      } else if (!window.lastTempLog || Date.now() - window.lastTempLog > 60000) {
        // Log temperature every minute
        addConsoleLog(`[${timestamp}] 🌡️ CPU temperature: ${tempData.temp}°C`, 'system');
        window.lastTempLog = Date.now();
      }
    }

  } catch (e) {
    // Silently handle errors for additional info to avoid spam
  }
}

// Admin bar functionality
async function fetchAdminBar() {
  try {
    const res = await fetch('/admin-status');
    const data = await res.json();

    document.getElementById('bar-ssid').textContent = data.ssid || '—';
    document.getElementById('bar-wifi-pass').textContent = data.wifiPassword || '—';
    document.getElementById('bar-users').textContent =
      typeof data.users === 'number' ? `${data.users}` : '0';

  } catch (e) {
    console.error('Could not load admin bar:', e);
  }
}

// Temperature monitoring
let showFahrenheit = false;

async function updateTemp() {
  try {
    const res = await fetch('/cpu-temp');
    const data = await res.json();
    const temp = data.temperature || 0;
    
    let displayTemp = temp;
    let unit = "°C";
    
    if (showFahrenheit) {
      displayTemp = (temp * 9/5) + 32;
      unit = "°F";
    }

    const tempBtn = document.getElementById('cpu-temp');
    tempBtn.textContent = `🌡️ ${displayTemp.toFixed(1)} ${unit}`;

    // Color coding based on temperature
    tempBtn.classList.remove('btn-success', 'btn-warning', 'btn-danger', 'btn-secondary');
    if (temp < 60) tempBtn.classList.add('btn-success');
    else if (temp < 70) tempBtn.classList.add('btn-warning');
    else if (temp < 75) tempBtn.classList.add('btn-warning');
    else tempBtn.classList.add('btn-danger');

  } catch (e) {
    console.warn('Failed to fetch temp:', e);
  }
}

// Authentication system
async function requireAdminAuth(passedSettings) {
  const settings = passedSettings;
  const overlay = document.getElementById('auth-overlay');
  const passwordInput = document.getElementById('auth-password');
  const submitBtn = document.getElementById('auth-submit');
  const errorDiv = document.getElementById('auth-error');

  if (!overlay || !passwordInput || !submitBtn) {
    console.warn('requireAdminAuth: overlay or controls not present in DOM. Skipping auth overlay.');
    return;
  }

  // Check if password is disabled
  if (!settings || !settings.hasOwnProperty('adminPassword') ||
      settings.adminPassword === null || settings.adminPassword === '' || settings.adminPassword === 'null') {
    console.debug('requireAdminAuth: admin password explicitly disabled on server — skipping auth.');
    overlay.classList.add('hidden');
    return;
  }

  // Check for cached session key
  const cachedKey = sessionStorage.getItem('nomad_admin_key');
  if (cachedKey && cachedKey === settings.adminPassword) {
    console.debug('requireAdminAuth: session key valid, skipping auth overlay.');
    overlay.classList.add('hidden');
    return;
  }

  // Show auth overlay if password is set and no valid session key
  console.debug('requireAdminAuth: admin password is set — showing auth overlay.');
  overlay.classList.remove('hidden');
  passwordInput.focus();

  // Handle authentication - only attach handlers once
  if (!submitBtn._authHandlerAttached) {
    const authenticate = async () => {
      const inputPw = passwordInput.value.trim();
      if (!inputPw) return;

      try {
        const hashedInput = await sha256Hex(inputPw);
        
        if (hashedInput === settings.adminPassword) {
          // Success
          sessionStorage.setItem('nomad_admin_key', hashedInput);
          sessionStorage.removeItem('nomad_force_reauth');
          overlay.classList.add('hidden');
          if (errorDiv) errorDiv.classList.add('hidden');
          passwordInput.value = '';
          console.debug('requireAdminAuth: authentication success, overlay hidden.');
        } else {
          // Failure
          if (errorDiv) errorDiv.classList.remove('hidden');
          passwordInput.value = '';
          passwordInput.focus();
        }
      } catch (e) {
        console.error('Authentication error:', e);
        if (errorDiv) errorDiv.classList.remove('hidden');
      }
    };

    submitBtn.onclick = authenticate;
    passwordInput.onkeydown = (e) => {
      if (e.key === 'Enter') authenticate();
    };
    submitBtn._authHandlerAttached = true;
  }
}
// Initialize everything when DOM is ready
document.addEventListener('DOMContentLoaded', async () => {
  // Initialize console
  addConsoleLog('Admin panel initialized', 'info');
  
  // Load settings and authenticate
  await loadSettings();
  
  // Fetch initial data
  fetchSD();
  fetchAdminBar();
  updateTemp();
  refreshConsole();
  
  // Set up RGB controls
  const colorPicker = document.getElementById('led-color');
  const modeButtons = {
    off: document.getElementById('mode-off'),
    solid: document.getElementById('mode-solid'),
    rainbow: document.getElementById('mode-rainbow')
  };

  // Color picker event
  if (colorPicker) {
    colorPicker.addEventListener('input', () => {
      if (ledMode === 'solid') {
        sendColorToServer(colorPicker.value);
      }
    });
  }

  // Mode button events
  if (modeButtons.off) {
    modeButtons.off.addEventListener('click', async () => {
      updateModeUI('off');
      await sendModeToServer('off');
      await saveSettings();
    });
  }

  if (modeButtons.solid) {
    modeButtons.solid.addEventListener('click', async () => {
      updateModeUI('solid');
      await sendModeToServer('solid');
      await sendColorToServer(colorPicker.value);
      await saveSettings();
    });
  }

  if (modeButtons.rainbow) {
    modeButtons.rainbow.addEventListener('click', async () => {
      updateModeUI('rainbow');
      await sendModeToServer('rainbow');
      await saveSettings();
    });
  }

  // Action button events
  const restartBtn = document.getElementById('btn-restart');
  if (restartBtn) {
    restartBtn.addEventListener('click', async () => {
      if (!confirm('⚠️ Are you sure you want to restart the device?')) return;
      try {
        await fetch('/restart', { method: 'POST' });
        addConsoleLog('Restart command sent', 'info');
        alert('Restart command sent. The device will reboot shortly.');
      } catch {
        addConsoleLog('Device disconnected - restart in progress', 'warning');
        alert('Device Disconnected, Please Reconnect (Successful Reboot)');
      }
    });
  }

  const shutdownBtn = document.getElementById('btn-shutdown');
  if (shutdownBtn) {
    shutdownBtn.addEventListener('click', async () => {
      const ok = confirm('⚠️ Shut down Nomad safely?\n\nThis will unmount the SD card and enter deep sleep.');
      if (!ok) return;

      try {
        const res = await fetch('/shutdown');
        if (res.ok) {
          addConsoleLog('Safe shutdown initiated', 'info');
          alert('Safe shutdown initiated.\nNomad will power down shortly.');
        } else {
          addConsoleLog('Shutdown command failed', 'error');
          alert('Shutdown completed successfully.');
        }
      } catch (err) {
        addConsoleLog('Device disconnected - shutdown in progress', 'warning');
        alert('Device disconnected — shutdown likely in progress.');
      }
    });
  }

  const usbBtn = document.getElementById('btn-usbmode');
  if (usbBtn) {
    usbBtn.addEventListener('click', async () => {
      const ok = confirm(
        '⚠️ This will restart the device into USB Transfer mode. It can take more than 60 seconds to mount\n\n' +
        'To exit USB mode, you must unplug and re-plug the device.\n\n' +
        'Proceed?'
      );
      if (!ok) return;
      try {
        await fetch('/enterUsb', { method: 'POST' });
        addConsoleLog('USB mode command sent', 'info');
        alert('USB-mode command sent. The device will reboot into USB MSC mode.');
      } catch {
        addConsoleLog('Web server closed - USB mode starting', 'warning');
        alert('Web server closed. Please check your computer in ~60 seconds.');
      }
    });
  }

  const flashBtn = document.getElementById('btn-flashmode');
  if (flashBtn) {
    flashBtn.addEventListener('click', async () => {
      const ok = confirm('⚠️ Enter flash mode? This will restart the device for firmware updates.');
      if (!ok) return;
      try {
        await fetch('/flash-mode', { method: 'POST' });
        addConsoleLog('Flash mode command sent', 'info');
        alert('Flash mode activated. Device will restart for firmware updates.');
      } catch {
        addConsoleLog('Flash mode activation failed', 'error');
        alert('Flash mode activated successfully. Device restarting...');
      }
    });
  }

  // Temperature click to toggle units
  const tempBtn = document.getElementById('cpu-temp');
  if (tempBtn) {
    tempBtn.addEventListener('click', () => {
      showFahrenheit = !showFahrenheit;
      updateTemp();
    });
  }

  // Auto-generate toggle
  const autoToggle = document.getElementById('auto-generate');
  if (autoToggle) {
    autoToggle.addEventListener('change', () => {
      isAutoGenerate = autoToggle.checked;
      saveSettings();
      addConsoleLog(`Auto-indexing ${isAutoGenerate ? 'enabled' : 'disabled'}`, 'info');
    });
  }

  // Disable password button
  const disablePasswordBtn = document.getElementById('btn-disable-password');
  if (disablePasswordBtn) {
    disablePasswordBtn.addEventListener('click', async () => {
      if (!confirm('Are you sure you want to disable the admin password?')) return;
      
      try {
        const settingsUpdate = {
          adminPassword: ""
        };

        const res = await fetch('/settings', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({
            'body': JSON.stringify(settingsUpdate)
          })
        });

        if (res.ok) {
          alert('Admin password disabled successfully');
          localStorage.removeItem('nomad_admin_logged_in');
          sessionStorage.removeItem('nomad_admin_key');
          location.reload();
        } else {
          alert('Failed to disable admin password');
        }
      } catch (e) {
        console.error('Error disabling admin password:', e);
        alert('Error disabling admin password');
      }
    });
  }

  // LCD toggle button
  const lcdToggleBtn = document.getElementById('lcd-toggle');
  if (lcdToggleBtn) {
    lcdToggleBtn.addEventListener('click', async () => {
      try {
        // Get current brightness value
        const brightnessElement = document.getElementById('brightness');
        const currentBrightness = parseInt(brightnessElement.value);

        // If brightness is 0, turn it back on to previous value or default
        if (currentBrightness === 0) {
          const stored = localStorage.getItem('lcd_brightness');
          const newBrightness = stored !== null ? parseInt(stored) : 100;
          await setBrightness(newBrightness);
          brightnessElement.value = newBrightness;
          updateBrightnessLabel(newBrightness);
        } else {
          // Turn off by setting brightness to 0 (store current)
          localStorage.setItem('lcd_brightness', currentBrightness);
          await setBrightness(0);
          brightnessElement.value = 0;
          updateBrightnessLabel(0);
        }
      } catch (e) {
        console.error('Failed to toggle LCD:', e);
      }
    });
  }

  // Set up periodic updates
  setInterval(fetchAdminBar, 30000); // Every 30 seconds
  setInterval(updateTemp, 6000); // Every 6 seconds
  setInterval(refreshConsole, 10000); // Every 10 seconds
  setInterval(fetchSD, 60000); // Every minute
});

// Global functions for HTML onclick handlers
window.updateAdminPassword = updateAdminPassword;
window.updateWiFiSettings = updateWiFiSettings;
window.setBrightness = setBrightness;
window.updateBrightnessLabel = updateBrightnessLabel;
window.generateMediaJson = generateMediaJson;
window.refreshConsole = refreshConsole;

// Also expose utility/init functions that may be called from HTML or externally
window.fetchSD = fetchSD;
window.applyThemeFlag = applyThemeFlag;
window.initTheme = initTheme;


function checkScreenSize() {
  const fileBrowserSection = document.querySelector('.file-browser-section');
  if (fileBrowserSection) {
    if (window.innerWidth <= 768) {
      fileBrowserSection.style.display = 'none';
    } else {
      fileBrowserSection.style.display = 'block';
    }
  }
}

window.addEventListener('resize', checkScreenSize);
window.addEventListener('load', checkScreenSize);
