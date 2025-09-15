    async function fetchSD() {
      try {
        const res = await fetch('/sdinfo');
        const { total, used } = await res.json();
        document.getElementById('sd-total').textContent = `Total: ${(total/1e9).toFixed(2)} GB`;
        document.getElementById('sd-used').textContent = `Used: ${(used/1e9).toFixed(2)} GB`;
        const percent = Math.round((used / total) * 100);
        document.getElementById('sd-bar').value = percent;
        document.getElementById('sd-percent').textContent = `${percent}%`;
      } catch {}
    }
/* ---------------- THEME: respect menu-controlled dark mode (safe, DOM-ready) ---------------- */
const THEME_KEY = 'nomad_dark_mode';

function applyThemeFlag(dark){
  try {
    dark = !!dark;

    // toggle class hook
    if (document.body) document.body.classList.toggle('dark', dark);

    // update CSS tokens on :root for wide coverage
    const root = document.documentElement.style;
    if (dark) {
      root.setProperty('--bg', '#0b1116');
      root.setProperty('--text', '#e6eef8');
      root.setProperty('--muted', '#9aa6b3');
      root.setProperty('--card-bg', '#071018');
      root.setProperty('--card-border', 'rgba(255,255,255,0.03)');
      root.setProperty('--header-text', '#e6eef8');
      // keep logo/primary as-is unless you want to change it
      // root.setProperty('--primary', '#4da3ff');
    } else {
      root.setProperty('--bg', '#f4f4f9');
      root.setProperty('--text', '#333');
      root.setProperty('--muted', '#6b7280');
      root.setProperty('--card-bg', '#ffffff');
      root.setProperty('--card-border', '#e6e9ef');
      root.setProperty('--header-text', '#ffffff');
      // root.setProperty('--primary', '#007bff');
    }

    // Ensure search stays readable
    const search = document.querySelector('.search-input') || document.querySelector('input[type="search"]');
    if (search) {
      try { search.style.background = '#fff'; search.style.color = '#000'; } catch (e) {}
    }
  } catch (e) {
    console.warn('applyThemeFlag failed', e);
  }
}

// Apply only after DOM is ready — prevents timing race that made the page appear dark prematurely
function initTheme() {
  try {
    const saved = localStorage.getItem(THEME_KEY);
    const isDark = saved === 'true';
    applyThemeFlag(isDark);
  } catch (e) {
    applyThemeFlag(false);
  }
}
// Run on DOMContentLoaded (and immediately if DOM already ready)
if (document.readyState === 'loading') {
  window.addEventListener('DOMContentLoaded', initTheme);
} else {
  initTheme();
}

// Sync across tabs when menu toggles the flag
window.addEventListener('storage', (ev) => {
  if (ev.key === THEME_KEY) {
    const dark = ev.newValue === 'true';
    applyThemeFlag(dark);
  }
});
// SHA-256 helper: returns hex string of input
async function sha256Hex(str) {
  if (!window.crypto || !crypto.subtle) {
    // very old env fallback (not expected in modern browsers)
    console.warn('SubtleCrypto unavailable; sha256Hex will return raw string (less secure).');
    return str;
  }
  const enc = new TextEncoder();
  const data = enc.encode(str || '');
  const hash = await crypto.subtle.digest('SHA-256', data);
  const bytes = new Uint8Array(hash);
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
}
async function loadSettings() {
  console.log("loadSettings() fired");
  try {
    const res = await fetch('/settings', { cache: 'no-store' });
    const s   = await res.json();
    console.log("settings from backend:", s);

    // --- Admin password sync / detection ---
    try {
      if (s.hasOwnProperty('adminPassword')) {
        const serverPw = s.adminPassword;

        // Hash for comparison (if crypto available)
        let serverHash;
        try {
          serverHash = await sha256Hex(serverPw);
        } catch (e) {
          console.warn("sha256Hex failed, falling back to plain pw compare:", e);
          serverHash = serverPw;
        }

        const storedHash = localStorage.getItem('nomad_admin_pw_hash') || null;
        if (!storedHash || storedHash !== serverHash) {
          // password has changed, or not stored yet
          localStorage.removeItem('nomad_admin_logged_in');
          localStorage.removeItem('nomad_admin_pw_hash');
          console.log('🔑 Admin password changed or not cached — forcing re-login.');
        }
        // (note: don’t save hash here; only after a successful login)
      } else {
        console.log("ℹ️ No adminPassword in settings — treating as password disabled.");
        localStorage.removeItem('nomad_admin_logged_in');
        localStorage.removeItem('nomad_admin_pw_hash');
      }
    } catch (e) {
      console.warn('Password check failed:', e);
    }

    // ── Wi-Fi SSID ────
    const ssidEl = document.getElementById('ssid');
    if (ssidEl) ssidEl.value = s.wifiSSID || '';

    // ── Wi-Fi Password ───
    const wifiPassEl = document.getElementById('wifi-password');
    if (wifiPassEl) wifiPassEl.value = s.wifiPassword || '';

    // ── RGB Settings ────
    console.log('ℹ️ loadSettings rgbMode:', s.rgbMode, 'rgbColor:', s.rgbColor);
    ledMode = s.rgbMode || 'off';
    updateModeUI(ledMode);
    const ledColorEl = document.getElementById('led-color');
    if (ledColorEl) ledColorEl.value = s.rgbColor || '#ff0000';

    // ── Brightness Mapping ──
    console.log('⚙️ Loaded brightness from settings.json:', s.brightness);
    let stored = (typeof s.brightness === 'number')
      ? Math.min(Math.max(s.brightness, 0), 90)
      : 45;
    const sliderVal = Math.round((stored / 90) * 99) + 1;
    const brEl = document.getElementById('brightness');
    if (brEl) {
      brEl.value = sliderVal;
      updateBrightnessLabel(sliderVal);
    }

    // ── Auto-Generate Media Toggle ───
    isAutoGenerate = !!s.autoGenerateMedia;
    updateToggle();
    const autoGenEl = document.getElementById('auto-generate');
    if (autoGenEl) autoGenEl.checked = isAutoGenerate;

    return s;

  } catch (e) {
    console.error('loadSettings failed:', e);
    return null;
  }
}


    // Gather all settings from UI and POST to /settings
// Gather all settings from UI and POST to /settings
async function saveSettings() {
  // read slider 1–100
  const sliderVal = parseInt(document.getElementById('brightness').value, 10);
  // map to 0–90
  const backendBrightness = Math.round((sliderVal / 100) * 90);

  const payload = {
    wifiSSID:           document.getElementById('ssid').value,
    // wifiPassword is handled by updateWiFiSettings()
    rgbMode:            ledMode,                      // current mode ('off','solid','rainbow')
    rgbColor:           document.getElementById('led-color').value,
    brightness:         backendBrightness,
    autoGenerateMedia:  isAutoGenerate
  };

  // include Wi-Fi password only if the user just updated it
  if (window._pendingWifiPassword) {
    payload.wifiPassword = window._pendingWifiPassword;
  }

  // include adminPassword only if set via updateAdminPassword()
  // Note: we intentionally leave window._pendingAdminPassword in place until POST completes.
  if (window._pendingAdminPassword !== undefined) {
    payload.adminPassword = window._pendingAdminPassword;
  }

  const body = encodeURIComponent(JSON.stringify(payload));

  try {
    const res = await fetch('/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: `body=${body}`
    });

    if (!res.ok) {
      console.error('saveSettings failed:', await res.text());
      return false;
    }

    // Success — clear pending values now that server accepted them
    window._pendingWifiPassword = null;
    // For admin password we set to undefined to indicate there is nothing pending now
    window._pendingAdminPassword = undefined;

    console.log('saveSettings: settings saved successfully.');
    return true;
  } catch (err) {
    console.error('saveSettings error:', err);
    return false;
  }
}

    function setRGBMode(mode) {
      updateModeUI(mode);       // highlight the correct button
      ledMode = mode;           // track current mode
      sendModeToServer(mode);   // immediately apply on-device
      saveSettings();           // persist to settings.json
    }

    function setSolidColor(color) {
      // update picker UI
      document.getElementById('led-color').value = color;
      sendColorToServer(color);  // immediately apply on device
      saveSettings();            // persist to settings.json
    }

async function updateAdminPassword() {
  const pass    = document.getElementById('new-password').value;
  const confirm = document.getElementById('confirm-password').value;
  if (pass !== confirm) {
    alert('Passwords do not match.');
    return;
  }

  // stash for saveSettings()
  window._pendingAdminPassword = pass;

  // Attempt to save to backend and wait for result
  const ok = await saveSettings();

  if (!ok) {
    alert('Failed to update admin password — please try again.');
    return;
  }

  // Clear cached login so next load (and new sessions) require auth
  localStorage.removeItem('nomad_admin_logged_in');
  localStorage.removeItem('nomad_admin_pw_hash');

  // Set a session-level force flag so the just-opened page will require reauth for testing
  sessionStorage.setItem('nomad_force_reauth', '1');

  // Inform the user
  alert('Admin password updated.');

  // Reload fresh settings and immediately require auth so the overlay appears
  const fresh = await loadSettings();
  if (typeof requireAdminAuth === 'function') {
    await requireAdminAuth(fresh);
  } else {
    // fallback: reload page to ensure fresh state
    location.reload();
  }
}

    document.getElementById("btn-disable-password").addEventListener("click", () => {
      if (confirm("Are you sure you want to disable the admin password?\nThis will make the admin panel accessible without authentication.")) {
        window._pendingAdminPassword = "";
        localStorage.removeItem("nomad_admin_logged_in");  
        saveSettings();
        alert("Admin password disabled.");
      }
    });
    function updateWiFiSettings() {
      const newSSID = document.getElementById('ssid').value;
      const newPass = document.getElementById('wifi-password').value;

      // Validate Wi-Fi password length
      if (newPass && newPass.length < 7) {
        alert("⚠️ Wi-Fi password must be at least 7 characters long.");
        return;
      }

      if (!confirm(
        "⚠️ Changing Wi-Fi requires a restart.\n\n" +
        "Are you sure you want to apply these changes?"
      )) {
        return;  
      }

      // stash the password…
      window._pendingWifiPassword = newPass;

      // immediately apply if WiFi API is present (for dev/testing)
      if (typeof WiFi !== 'undefined' && WiFi.softAP) {
        WiFi.softAP(newSSID, newPass);
      }

      // persist everything
      saveSettings();
    }



    async function fetchWiFiSettings() {
      try {
        const res = await fetch('/wifi-settings');
        const data = await res.json();
        document.getElementById('auto-generate').checked = data.autoGenerate || false;
      } catch {}
    }

    function setBrightness(val) {
      console.log('Set brightness to:', val);
      saveSettings();  // persist new brightness
    }

    async function generateMediaJson() {
      if (!confirm("⚠️ This will regenerate re-index the device library.\n\nYou may experiance minor lag.\n\nProceed?")) {
        return; // User canceled
      }

      const msg = document.getElementById('generate-msg');
      msg.textContent = 'indexing...';

      try {
        // Trigger the POST request; device will reboot, so no reliable response expected
        await fetch('/generate-media', { method: 'POST' });

        // Optionally clear or update message here, but device reboot means page will reload later
        msg.textContent = 'indexing started.';
      } catch {
        msg.textContent = 'Reconnect.';
      }
    }


    function toggleAutoGenerate(enabled) {
      saveSettings(); 
    }

    fetchSD();
    fetchWiFiSettings();
    setInterval(fetchSD, 30000);

// LED Control Elements
const colorPicker = document.getElementById('led-color');
const modeButtons = {
  off: document.getElementById('mode-off'),
  solid: document.getElementById('mode-solid'),
  rainbow: document.getElementById('mode-rainbow'),
};

let ledMode = 'off';

function updateModeUI(mode) {
  ledMode = mode;
  for (const [key, btn] of Object.entries(modeButtons)) {
    if (key === mode) {
      btn.classList.add('active');
      btn.setAttribute('aria-pressed', 'true');
    } else {
      btn.classList.remove('active');
      btn.setAttribute('aria-pressed', 'false');
    }
  }
  colorPicker.disabled = (mode !== 'solid');
}

async function sendModeToServer(mode) {
  if (mode === 'off') {
    await fetch('/led/onoff', {
      method: 'POST',
      body: JSON.stringify({ enabled: false }),
      headers: { 'Content-Type': 'application/json' }
    });
  } else if (mode === 'solid') {
    await fetch('/led/onoff', {
      method: 'POST',
      body: JSON.stringify({ enabled: true }),
      headers: { 'Content-Type': 'application/json' }
    });
    await sendColorToServer(colorPicker.value);
  } else if (mode === 'rainbow') {
    await fetch('/led/onoff', {
      method: 'POST',
      body: JSON.stringify({ enabled: true }),
      headers: { 'Content-Type': 'application/json' }
    });
    await fetch('/led/rainbow', { method: 'POST' });
  }
}

async function sendColorToServer(color) {
  await fetch('/led/color', {
    method: 'POST',
    body: JSON.stringify({ color }),
    headers: { 'Content-Type': 'application/json' }
  });
}

// Event Listeners
colorPicker.addEventListener('input', () => {
  if (ledMode === 'solid') {
    sendColorToServer(colorPicker.value);
  }
});

modeButtons.off.addEventListener('click', async () => {
  updateModeUI('off');
  await sendModeToServer('off');
});

modeButtons.solid.addEventListener('click', async () => {
  updateModeUI('solid');
  await sendModeToServer('solid');
});

modeButtons.rainbow.addEventListener('click', async () => {
  updateModeUI('rainbow');
  await sendModeToServer('rainbow');
});

// Initialize UI state on load
updateModeUI('off');
function updateBrightnessLabel(val) {

  document.getElementById('brightness-percent').textContent = `${val}%`;
}
document.addEventListener('DOMContentLoaded', async () => {
  // load settings first so we know if password is configured
  const settings = await loadSettings();

  // require admin auth if needed (shows overlay if password set)
  await requireAdminAuth(settings);

  // --- existing initialization code (unchanged) ---
  // Attach RGB button clicks
  document.getElementById('mode-off').onclick    = () => setRGBMode('off');
  document.getElementById('mode-solid').onclick  = () => setRGBMode('solid');
  document.getElementById('mode-rainbow').onclick= () => setRGBMode('rainbow');

  // Attach color-picker change
  document.getElementById('led-color').onchange = e => setSolidColor(e.target.value);
  const brightnessSlider = document.getElementById('brightness');
  updateBrightnessLabel(brightnessSlider.value);

  async function fetchAdminBar() {
    try {
      const res = await fetch('/admin-status');
      const data = await res.json();

      document.getElementById('bar-ssid').textContent      = data.ssid      || '—';
      document.getElementById('bar-wifi-pass').textContent = data.wifiPassword || '—';
      document.getElementById('bar-users').textContent =
        typeof data.users === 'number' ? `${data.users} connected` : 'none';

    } catch (e) {
      console.error('Could not load admin bar:', e);
    }
  }

  // Hook restart button
  document.getElementById('btn-restart').addEventListener('click', async () => {
    if (!confirm('⚠️ Are you sure you want to restart the device?')) return;
    try {
      await fetch('/restart', { method: 'POST' });
      alert('Restart command sent. The device will reboot shortly.');
    } catch {
      alert('Device Disconnected, Please Reconnect (Successful Reboot)');
    }
  });

  // USB MODE button
  document.getElementById('btn-usbmode').addEventListener('click', async () => {
    const ok = confirm(
      '⚠️ This will restart the device into USB Transfer mode. It can take more than 60 secounds to mount\n\n' +
      'To exit USB mode, you must unplug and re-plug the device.\n\n' +
      'Proceed?'
    );
    if (!ok) return;
    try {
      await fetch('/enterUsb', { method: 'POST' });
      alert('USB-mode command sent. The device will reboot into USB MSC mode.');
    } catch {
      alert('Web server closed. Please check your computor in ~60 secounds.');
    }
  });

  // Fetch bar info on load & every 30s
  fetchAdminBar();
  setInterval(fetchAdminBar, 30000);
  updateTemp();
});
document.getElementById('cpu-temp').addEventListener('click', () => {
  showFahrenheit = !showFahrenheit;
  updateTemp();
});

async function requireAdminAuth(passedSettings) {
  // helper: pick last element if duplicate IDs exist (avoids ambiguous node returns)
  function pickLast(selector) {
    const els = document.querySelectorAll(selector);
    return els.length ? els[els.length - 1] : null;
  }

  // Prefer the new unique IDs but fall back to old names if present
  const pwOverlay = pickLast('#admin-pw-overlay') || pickLast('#pw-overlay');
  const pwInputEl = pickLast('#admin-pw-input')   || pickLast('#pw-input');
  const pwErrorEl = pickLast('#admin-pw-error')   || pickLast('#pw-error');
  const pwSubmitEl= pickLast('#admin-pw-submit')  || pickLast('#pw-submit');

  // if any required DOM piece missing, log and skip (avoid uncaught exceptions)
  if (!pwOverlay || !pwInputEl || !pwSubmitEl) {
    console.warn('requireAdminAuth: overlay or controls not present in DOM. Skipping auth overlay.');
    return;
  }

  // detach old listeners by cloning nodes (prevents legacy handlers)
  function replaceNodeIfNeeded(el) {
    if (!el || !el.parentNode) return el;
    // clone to remove attached event listeners (if any)
    const clone = el.cloneNode(true);
    el.parentNode.replaceChild(clone, el);
    return clone;
  }
  let pwInput  = replaceNodeIfNeeded(pwInputEl);
  let pwSubmit = replaceNodeIfNeeded(pwSubmitEl);
  let pwError  = pwErrorEl;

  // block UI until auth succeeds
  document.documentElement.classList.add('admin-locked');

  function showOverlay() { pwOverlay.classList.add('visible'); }
  function hideOverlay() {
    pwOverlay.classList.remove('visible');
    document.documentElement.classList.remove('admin-locked');
  }

  // get freshest settings if possible (no-store)
  let settings = passedSettings || null;
  try {
    const r = await fetch('/settings', { cache: 'no-store' });
    if (r && r.ok) settings = await r.json();
  } catch (e) { /* ignore, use passedSettings if available */ }

  // DEBUG logs (useful while testing)
  console.debug('requireAdminAuth: settings present?', !!settings);
  if (settings) console.debug('requireAdminAuth: has adminPassword field?', Object.prototype.hasOwnProperty.call(settings, 'adminPassword'));
  console.debug('requireAdminAuth: localStorage.nomad_admin_logged_in =', localStorage.getItem('nomad_admin_logged_in'));
  console.debug('requireAdminAuth: sessionStorage.nomad_force_reauth =', sessionStorage.getItem('nomad_force_reauth'));

  const hasSettings = !!settings;
  const hasAdminField = hasSettings && Object.prototype.hasOwnProperty.call(settings, 'adminPassword');

  // explicit disabled password => skip overlay
  if (hasAdminField && (typeof settings.adminPassword === 'string') && settings.adminPassword.trim() === '') {
    localStorage.removeItem('nomad_admin_logged_in');
    localStorage.removeItem('nomad_admin_pw_hash');
    window.adminToken = null;
    hideOverlay();
    console.debug('requireAdminAuth: admin password explicitly disabled on server — skipping auth.');
    return;
  }

  // already logged in in this browser?
  const alreadyLogged = localStorage.getItem('nomad_admin_logged_in') === 'true';
  // session force flag overrides cached login (useful for testing after password change)
  const forceReauth = sessionStorage.getItem('nomad_force_reauth') === '1';

  if (alreadyLogged && !forceReauth) {
    hideOverlay();
    console.debug('requireAdminAuth: cached login found and no force flag — skipping overlay.');
    return;
  }

  // show overlay to block UI
  showOverlay();
  if (pwError) pwError.classList.add('hidden');
  if (pwInput) { pwInput.value = ''; pwInput.focus(); }

  async function checkWithServer(password) {
    try {
      const r = await fetch('/admin-check', { method: 'GET', headers: { 'X-Admin-Auth': password }});
      return r && r.ok;
    } catch (e) { return false; }
  }

  let tries = 0;

  async function submitHandler(e) {
    e && e.preventDefault();
    const attempt = pwInput ? pwInput.value : '';
    if (!attempt) {
      if (pwError) { pwError.textContent = 'Please enter a password.'; pwError.classList.remove('hidden'); }
      return;
    }

    let ok = false;

if (hasAdminField) {
  // refresh settings so we don’t compare against stale data
  let latestPw = '';
  try {
    const r = await fetch('/settings', { cache: 'no-store' });
    if (r && r.ok) {
      const fresh = await r.json();
      latestPw = (typeof fresh.adminPassword === 'string') ? fresh.adminPassword : '';
    }
  } catch (e) {
    console.warn('Could not refresh settings for login compare, falling back to cached one.');
    latestPw = (typeof settings.adminPassword === 'string') ? settings.adminPassword : '';
  }

  ok = attempt === latestPw;
} else {
  ok = true;
}


    if (ok) {
      // success — set logged-in flag and pw-hash for change-detection
      localStorage.setItem('nomad_admin_logged_in', 'true');
      if (hasAdminField) {
        try {
          const ph = await sha256Hex(latestPw || '');
          localStorage.setItem('nomad_admin_pw_hash', ph);
        } catch (e) {
          console.warn('requireAdminAuth: failed to store pw hash:', e);
        }
      }
      if (sessionStorage.getItem('nomad_force_reauth') === '1') {
        sessionStorage.removeItem('nomad_force_reauth');
      }

      window.adminToken = attempt; // legacy header support
      hideOverlay();
      pwSubmit.removeEventListener('click', submitHandler);
      pwInput.removeEventListener('keyup', keyHandler);
      console.debug('requireAdminAuth: authentication success, overlay hidden.');
      return;
    }


    // failure
    tries++;
    if (pwError) { pwError.textContent = 'Incorrect password, try again.'; pwError.classList.remove('hidden'); }
    if (pwInput) { pwInput.value = ''; pwInput.focus(); }
    if (tries >= 6) {
      pwSubmit.removeEventListener('click', submitHandler);
      pwInput.removeEventListener('keyup', keyHandler);
      alert('Too many incorrect attempts. Reloading page.');
      location.reload();
    }
  }

  function keyHandler(e) { if (e.key === 'Enter') submitHandler(); }

  pwSubmit.addEventListener('click', submitHandler);
  pwInput.addEventListener('keyup', keyHandler);
}


const toggleBtn = document.getElementById("auto-generate-toggle");
let isAutoGenerate = false;  

function updateToggle() {
  toggleBtn.textContent = isAutoGenerate ? "ON" : "OFF";
  toggleBtn.classList.toggle("off", !isAutoGenerate);
}


function initToggle() {
  updateToggle();
  document.getElementById('auto-generate').checked = isAutoGenerate;
}

toggleBtn.addEventListener("click", () => {
  isAutoGenerate = !isAutoGenerate;
  updateToggle();
  document.getElementById('auto-generate').checked = isAutoGenerate;
  saveSettings();
});

// Flash Mode button
document.getElementById("btn-flashmode").addEventListener("click", async () => {
  if (!confirm("⚠️ Enter Flash Mode?\n\nThis will put Nomad into flashing mode for uploading new firmware")) {
    return;
  }
  try {
    const res = await fetch("/flash-mode", { method: "POST" });
    if (res.ok) {
      alert("Device is now in Flash Mode.\nReconnect via USB to upload firmware.");
    } else {
      alert("Failed to enter Flash Mode. Check Nomad Screen, this could be a false read.");
    }
  } catch (err) {
    alert("Flash-mode command sent. If the page disconnected, the device is likely ready");
  }
});


let showFahrenheit = false;

async function updateTemp() {
  try {
    const res = await fetch('/cpu-temp');
    const { temp } = await res.json();

    const tempBtn = document.getElementById('cpu-temp');

    let displayTemp = temp;
    let unit = "°C";

    if (showFahrenheit) {
      displayTemp = (temp * 9/5) + 32;
      unit = "°F";
    }

    tempBtn.textContent = `Temp: ${displayTemp.toFixed(1)} ${unit}`;

    if (temp < 60) tempBtn.style.backgroundColor = '#28a745'; // Green
    else if (temp < 70) tempBtn.style.backgroundColor = '#ffc107'; // Yellow
    else if (temp < 75) tempBtn.style.backgroundColor = '#fd7e14'; // Orange
    else tempBtn.style.backgroundColor = '#dc3545'; // Red

  } catch (e) {
    console.warn('Failed to fetch temp:', e);
  }
}
 // Adaptive iframe height on resize
  (function () {
    const wrap = document.getElementById('filebrowser-frame-wrap');
    const iframe = document.getElementById('filebrowser-iframe');

    function resizeFrame() {
      // Example heuristic: make it 70% of viewport height for desktop, 55% for small screens
      const h = window.innerWidth < 768 ? Math.round(window.innerHeight * 0.55) : Math.round(window.innerHeight * 0.70);
      wrap.style.height = h + 'px';
      iframe.style.height = '100%';
    }
    window.addEventListener('resize', resizeFrame);
    window.addEventListener('load', resizeFrame);
    resizeFrame();

    // Optional postMessage listener — the embedded filebrowser can send: { action: 'reload-admin' }
    window.addEventListener('message', (ev) => {
      // Only accept messages from same origin (security)
      if (ev.origin !== window.location.origin) return;
      const data = ev.data || {};
      if (data.action === 'reload-admin') {
        // Reload the admin bar and SD info (adjust to your real functions)
        if (typeof fetchAdminBar === 'function') fetchAdminBar();
        if (typeof fetchSD === 'function') fetchSD();
      }
    });
  })();
// Refresh every 6 seconds
setInterval(updateTemp, 6000);