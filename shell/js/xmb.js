/* XrossOS Shell — XMB-навигация (PS3) + полноэкранные приложения.
   Управление: клавиатура, мышь, тач-свайпы, геймпад (Gamepad API), голос. */
let toastTimer;
function toast(s) {
  const el = document.getElementById('toast');
  el.textContent = s; el.classList.add('show');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.remove('show'), 2600);
}
const $ = (s) => document.querySelector(s);

window.XMB = (() => {
  let cat = 3, item = 0, appOpen = null; // старт — на «Музыке», как демо
  cat = 6; // «Сеть» по центру экрана при загрузке визуально приятнее; ниже render сам выровняет
  cat = 3;
  const catsEl = $('#cats'), itemsEl = $('#items');

  const itemsOf = (c) => XROSS_APPS.filter(a => a.cat === XROSS_CATEGORIES[c].id);

  function render() {
    catsEl.innerHTML = XROSS_CATEGORIES.map((c, i) =>
      `<div class="cat ${i === cat ? 'focus' : ''}" data-i="${i}">
         <span class="ic">${c.icon}</span><span class="nm">${c.title}</span></div>`).join('');
    // XMB-сдвиг: сфокусированная категория всегда по центру (как на PS3)
    const shift = (innerWidth / 2) - (cat * 132 + 66) - (catsEl.offsetWidth / 2 - innerWidth / 2);
    catsEl.style.transform = `translateX(${(cat - (XROSS_CATEGORIES.length - 1) / 2) * -132}px)`;
    const list = itemsOf(cat);
    item = Math.min(item, Math.max(0, list.length - 1));
    itemsEl.innerHTML = list.map((a, i) =>
      `<div class="item ${i === item ? 'focus' : ''}" data-i="${i}">
         <span class="ic" style="color:${a.color}">${a.icon}</span>
         <span><b>${a.title}</b><small>${a.sub}</small></span></div>`).join('')
      || '<div class="item focus"><span><b>Пусто</b><small>Здесь пока нет приложений</small></span></div>';
    catsEl.querySelectorAll('.cat').forEach(el => el.onclick = () => { cat = +el.dataset.i; item = 0; render(); });
    itemsEl.querySelectorAll('.item').forEach(el => el.onclick = () => { item = +el.dataset.i; render(); select(); });
  }

  function move(dx, dy) {
    if (appOpen) return;
    if (dx) { cat = (cat + dx + XROSS_CATEGORIES.length) % XROSS_CATEGORIES.length; item = 0; }
    if (dy) { const n = itemsOf(cat).length; item = (item + dy + n) % n; }
    render();
  }
  function select() { const a = itemsOf(cat)[item]; if (a) openApp(a); }
  function back() { if (appOpen) closeApp(); }

  /* ── Полноэкранный запуск ─────────────────── */
  function openApp(a) {
    appOpen = a;
    $('#app-head .ic').textContent = a.icon;
    $('#app-head b').textContent = a.title;
    $('#xmb').classList.add('hidden');
    const body = $('#app-body');
    body.innerHTML = '';
    (BUILTINS[a.builtin] || BUILTINS.stub)(body, a);
    $('#app').classList.add('open');
    $('#app').setAttribute('aria-hidden', 'false');
    document.documentElement.requestFullscreen?.().catch(() => {});
    toast('▶ ' + a.title + ' — на весь экран · Esc — выход');
  }
  function closeApp() {
    appOpen = null;
    $('#app').classList.remove('open');
    $('#xmb').classList.remove('hidden');
    if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
    stopMusic();
  }
  $('#app-close').onclick = closeApp;

  /* ── Управление ───────────────────────────── */
  addEventListener('keydown', (e) => {
    if (e.key === 'Escape') return back();
    if (appOpen) return;
    const k = { ArrowRight: [1, 0], ArrowLeft: [-1, 0], ArrowDown: [0, 1], ArrowUp: [0, -1] }[e.key];
    if (k) { e.preventDefault(); move(...k); }
    if (e.key === 'Enter') select();
  });
  addEventListener('wheel', (e) => { if (!appOpen) move(0, e.deltaY > 0 ? 1 : -1); }, { passive: true });
  let tx = 0, ty = 0;
  addEventListener('touchstart', e => { tx = e.touches[0].clientX; ty = e.touches[0].clientY; }, { passive: true });
  addEventListener('touchend', e => {
    if (appOpen) return;
    const dx = e.changedTouches[0].clientX - tx, dy = e.changedTouches[0].clientY - ty;
    if (Math.abs(dx) > 40 || Math.abs(dy) > 40)
      Math.abs(dx) > Math.abs(dy) ? move(Math.sign(dx) * -1, 0) : move(0, Math.sign(dy) * -1);
  }, { passive: true });

  // Геймпад: опрос крестовины/стиков, кнопка ✕ (0) — выбрать, ○ (1) — назад, PS (16) — домой
  let gpPrev = [];
  (function pollGamepad() {
    requestAnimationFrame(pollGamepad);
    const gp = navigator.getGamepads?.()[0]; if (!gp) return;
    const b = gp.buttons.map(x => x.pressed);
    const press = (i) => b[i] && !gpPrev[i];
    if (!appOpen) {
      if (press(15)) move(1, 0); if (press(14)) move(-1, 0);
      if (press(13)) move(0, 1); if (press(12)) move(0, -1);
      if (press(0)) select();
    }
    if (press(1) || press(16)) back();
    gpPrev = b;
  })();

  /* ══ Встроенные приложения (каждое — fullscreen) ══ */
  let audio = null, musicTimer = null;
  function stopMusic() { clearInterval(musicTimer); audio?.close(); audio = null; }

  const BUILTINS = {
    stub(el, a) { el.innerHTML = `<div class="card"><h2>${a.icon} ${a.title}</h2><p style="margin-top:10px;opacity:.8">Приложение установлено как внешний пакет ОС и запускается композитором Cage в полноэкранном режиме.</p></div>`; },

    profile(el) { el.innerHTML = `<div class="card" style="text-align:center;padding:48px">
      <div style="font-size:76px">👤</div><h2 style="margin:12px">Гость</h2>
      <p style="opacity:.8">Вход выполнен локально · трофеев: 3 · время в системе: 12 ч</p>
      <p style="margin-top:16px"><button class="btn" onclick="toast('Смена пользователя — в сборке ОС через greetd')">Сменить пользователя</button></p></div>`; },

    trophies(el) { el.innerHTML = `<h2 style="margin-bottom:16px">🏆 Достижения</h2>` +
      [['Первый запуск', 'Открыть оболочку XrossOS', true], ['Голос!', 'Первая голосовая команда', true],
       ['Меломан', 'Послушать все треки', true], ['Спидраннер', 'Пройти «Пузыри» за 30 секунд', false]]
        .map(t => `<div class="card row"><span>${t[2] ? '🥇' : '🔒'} <b>${t[0]}</b><br><small style="opacity:.7">${t[1]}</small></span><span>${t[2] ? 'Получено' : ''}</span></div>`).join(''); },

    settings(el) {
      const themes = [[222, 'Синяя волна'], [265, 'Фиолет PS Vita'], [190, 'Бирюза PSP'], [330, 'Розовая'], [150, 'Изумруд'], [20, 'Закат']];
      el.innerHTML = `<h2 style="margin-bottom:16px">⚙️ Настройки системы</h2>
        <div class="card"><b>Тема волны (как на PS3)</b><div class="swatches" style="margin-top:14px">
          ${themes.map(t => `<div class="swatch ${+getComputedStyle(document.documentElement).getPropertyValue('--hue') === t[0] ? 'on' : ''}"
            title="${t[1]}" data-hue="${t[0]}" style="background:hsl(${t[0]} 80% 55%)"></div>`).join('')}</div></div>
        <div class="card row"><span><b>Плавные анимации</b><br><small style="opacity:.7">Отключите для слабого железа</small></span><span class="switch ${!document.body.classList.contains('reduce-motion') ? 'on' : ''}" id="sw-anim"></span></div>
        <div class="card row"><span><b>Озвучивание ответов (TTS)</b><br><small style="opacity:.7">Голосовой помощник говорит вслух</small></span><span class="switch ${localStorage.xross_tts !== '0' ? 'on' : ''}" id="sw-tts"></span></div>
        <div class="card row"><span><b>Полный экран для всех приложений</b><br><small style="opacity:.7">В ОС принудительно: Cage (Wayland), выключить нельзя</small></span><span class="switch on"></span></div>`;
      el.querySelectorAll('.swatch').forEach(s => s.onclick = () => {
        document.documentElement.style.setProperty('--hue', s.dataset.hue);
        localStorage.xross_hue = s.dataset.hue; openApp(XROSS_APPS.find(a => a.id === 'settings')); toast('Тема: ' + s.title);
      });
      $('#sw-anim').onclick = (e) => { document.body.classList.toggle('reduce-motion'); e.target.classList.toggle('on'); };
      el.querySelector('#sw-tts').onclick = (e) => { localStorage.xross_tts = localStorage.xross_tts === '0' ? '1' : '0'; e.target.classList.toggle('on'); };
    },

    voice(el) { el.innerHTML = `<div class="card" style="text-align:center;padding:44px">
      <div style="font-size:70px">🎤</div><h2 style="margin:14px">Голосовое управление</h2>
      <p style="opacity:.85;line-height:1.7">Нажмите кнопку и говорите по-русски:<br>
      «открой музыку» · «открой настройки» · «вниз» · «выбери» · «домой» · «который час» · «помощь»</p>
      <p style="margin-top:20px"><button class="btn" onclick="Voice.toggle()">🎤 Включить / выключить микрофон (V)</button></p>
      <p style="opacity:.6;margin-top:16px;font-size:13px">В браузере нужен Chrome/Edge и доступ к микрофону. В образе ОС — офлайн Vosk, интернет не нужен.</p></div>`; },

    about(el) { el.innerHTML = `<div class="card"><h2>✦ XrossOS 0.1 «Wave»</h2>
      <p style="margin-top:12px;line-height:1.8;opacity:.9">База: Alpine Linux 3.20 (musl, BusyBox) · ядро LTS<br>
      Композитор: Cage (Wayland, kiosk) — каждое приложение на весь экран<br>
      Оболочка: XMB в стиле PS3 / PS Vita / PSP, Canvas-волна, WebAudio<br>
      Голос: Web Speech API в браузере / Vosk офлайн в образе ОС<br>
      Образ: ~180 МБ · ОЗУ оболочки: ~120 МБ · загрузка: ~6 с</p></div>`; },

    photos(el) { const g = ['#ff5c7a,#7c5cff', '#4cc9f0,#38d39f', '#f5b301,#ff8a5c', '#7c5cff,#ff7ac6', '#38d39f,#4cc9f0', '#ff8a5c,#f5b301'];
      el.innerHTML = `<h2 style="margin-bottom:16px">🖼️ Фотографии</h2><div class="grid">
      ${g.map((c, i) => `<div class="tile" style="background:linear-gradient(135deg,${c})" onclick="toast('Снимок ${i + 1} — просмотр на весь экран уже включён 😄')">🌄</div>`).join('')}</div>`; },

    music(el) {
      const tracks = [['Волна океана', [262, 330, 392, 330]], ['Неоновый город', [392, 440, 523, 440]], ['Звёздная пыль', [523, 494, 440, 392]]];
      el.innerHTML = `<h2 style="margin-bottom:16px">🎵 Музыка</h2>
        ${tracks.map((t, i) => `<div class="card row"><span><b>${t[0]}</b><br><small style="opacity:.7">Синтезатор XrossOS</small></span>
          <button class="btn" data-t="${i}">▶ Слушать</button></div>`).join('')}
        <div class="card" style="text-align:center;font-size:34px;letter-spacing:6px" id="viz">▂ ▅ ▃ ▇ ▄</div>`;
      el.querySelectorAll('[data-t]').forEach(b => b.onclick = () => {
        stopMusic();
        audio = new (window.AudioContext || window.webkitAudioContext)();
        const notes = tracks[+b.dataset.t][1]; let n = 0;
        const viz = ['▂ ▅ ▃ ▇ ▄ ▆', '▅ ▃ ▇ ▄ ▆ ▂', '▃ ▇ ▄ ▆ ▂ ▅', '▇ ▄ ▆ ▂ ▅ ▃'];
        musicTimer = setInterval(() => {
          const o = audio.createOscillator(), gn = audio.createGain();
          o.frequency.value = notes[n % 4]; o.type = 'sine';
          gn.gain.setValueAtTime(.18, audio.currentTime); gn.gain.exponentialRampToValueAtTime(.001, audio.currentTime + .42);
          o.connect(gn).connect(audio.destination); o.start(); o.stop(audio.currentTime + .45);
          $('#viz').textContent = viz[n % 4]; n++;
        }, 430);
        toast('▶ ' + tracks[+b.dataset.t][0]);
      });
    },

    video(el) { el.innerHTML = `<h2 style="margin-bottom:14px">🎬 Видео — «Волна» (демо)</h2>
      <canvas id="vid" width="960" height="480" style="width:100%;border-radius:14px;background:#000"></canvas>
      <p style="margin-top:12px;opacity:.7">В сборке ОС видео играет mpv (аппаратное декодирование), тоже строго на весь экран.</p>`;
      const c = el.querySelector('#vid').getContext('2d'); let t = 0;
      (function f() { if (!appOpen || appOpen.id !== 'video') return; requestAnimationFrame(f); t += .03;
        c.fillStyle = '#05070f'; c.fillRect(0, 0, 960, 480);
        c.strokeStyle = '#6ea8ff'; c.lineWidth = 3; c.beginPath();
        for (let x = 0; x < 960; x += 5) { const y = 240 + Math.sin(x / 90 + t) * 70; x ? c.lineTo(x, y) : c.moveTo(x, y); } c.stroke();
        c.fillStyle = '#fff'; c.font = '28px sans-serif'; c.fillText('XrossOS · демо-видео', 30, 50); })(); },

    bubbles(el) {
      el.innerHTML = `<div class="row"><h2>🫧 Пузыри — лопни 10 штук!</h2><b id="score">0 / 10</b></div>
        <div id="field" style="position:relative;height:62vh;margin-top:14px"></div>`;
      const field = el.querySelector('#field'); let score = 0;
      const spawn = () => { if (!appOpen) return;
        const b = document.createElement('div'); b.className = 'bubble';
        const s = 46 + Math.random() * 56;
        b.style.cssText += `width:${s}px;height:${s}px;left:${Math.random() * 88}%;top:${Math.random() * 78}%`;
        b.textContent = ['🫧', '⭐', '💎'][Math.random() * 3 | 0]; b.style.fontSize = s / 2.4 + 'px';
        b.onclick = () => { b.remove(); score++; el.querySelector('#score').textContent = score + ' / 10';
          if (score >= 10) { toast('🏆 Трофей «Спидраннер» почти ваш!'); Voice.say('Победа! Все пузыри лопнули'); score = 0; } spawn(); };
        field.appendChild(b); setTimeout(() => { b.remove(); if (appOpen) spawn(); }, 4200); };
      for (let i = 0; i < 5; i++) spawn();
    },

    snake(el) { el.innerHTML = `<h2 style="margin-bottom:12px">🐍 Волна-змейка — стрелки / голос: «вверх, вниз…»</h2>
      <canvas id="sn" width="520" height="520" style="background:#0008;border-radius:14px;max-width:90vw"></canvas><p id="sn-score" style="margin-top:10px">Очки: 0 · Esc — выход</p>`;
      const c = el.querySelector('#sn').getContext('2d'); const N = 26, S = 20;
      let sn = [[8, 8]], d = [1, 0], food = [15, 10], pts = 0;
      addEventListener('keydown', window.__snk = (e) => { const m = { ArrowUp: [0, -1], ArrowDown: [0, 1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] }[e.key]; if (m && appOpen?.id === 'snake') d = m; });
      const iv = setInterval(() => { if (!appOpen || appOpen.id !== 'snake') return clearInterval(iv), removeEventListener('keydown', window.__snk);
        const h = [sn[0][0] + d[0], sn[0][1] + d[1]];
        if (h[0] < 0 || h[1] < 0 || h[0] >= N || h[1] >= N) { sn = [[8, 8]]; pts = 0; }
        sn.unshift(h);
        if (h[0] === food[0] && h[1] === food[1]) { pts++; food = [Math.random() * N | 0, Math.random() * N | 0]; el.querySelector('#sn-score').textContent = 'Очки: ' + pts; } else sn.pop();
        c.fillStyle = '#05070f'; c.fillRect(0, 0, 520, 520);
        c.fillStyle = '#ff5c7a'; c.fillRect(food[0] * S + 3, food[1] * S + 3, S - 6, S - 6);
        sn.forEach((p, i) => { c.fillStyle = `hsl(150 80% ${60 - i * 1.5}%)`; c.fillRect(p[0] * S + 2, p[1] * S + 2, S - 4, S - 4); });
      }, 130); },

    browser(el) { el.innerHTML = `<div class="row" style="gap:10px;margin-bottom:14px">
        <input id="url" value="https://example.org" style="flex:1"><button class="btn" id="go">Перейти</button></div>
      <div style="height:calc(100% - 60px)"><iframe id="fr" src="https://example.org"></iframe></div>
      <p style="opacity:.6;font-size:12.5px;margin-top:8px">Некоторые сайты запрещают встраивание — в сборке ОС браузер (Firefox ESR / Cog) открывается отдельным полноэкранным окном Cage.</p>`;
      el.querySelector('#go').onclick = () => { let u = el.querySelector('#url').value; if (!/^https?:/.test(u)) u = 'https://' + u; el.querySelector('#fr').src = u; }; },

    store(el) { const apps = [['Telegram', '💬'], ['RetroArch', '🕹️'], ['Kodi', '📺'], ['GIMP', '🎨']];
      el.innerHTML = `<h2 style="margin-bottom:16px">🛍️ Магазин приложений</h2><div class="grid">
      ${apps.map(a => `<div class="card" style="text-align:center"><div style="font-size:44px">${a[1]}</div><b>${a[0]}</b>
        <div class="progress" style="margin:12px 0;display:none"><i></i></div>
        <button class="btn ghost" style="width:100%">Установить</button></div>`).join('')}</div>
      <p style="opacity:.65;font-size:13px">В образе ОС установка = apk add, ярлык сам появляется в XMB, запуск — на весь экран.</p>`;
      el.querySelectorAll('.btn').forEach((b, i) => b.onclick = () => { const p = b.previousElementSibling; p.style.display = 'block';
        let w = 0; const iv = setInterval(() => { w += 20; p.firstElementChild.style.width = w + '%';
          if (w >= 100) { clearInterval(iv); b.textContent = '✓ Установлено'; toast(apps[i][0] + ' установлен'); } }, 220); }); },

    files(el) { el.innerHTML = `<h2 style="margin-bottom:16px">📁 Файлы — /home/guest</h2>` +
      [['📁', 'Фото', '12 файлов'], ['📁', 'Музыка', '3 файла'], ['📁', 'Загрузки', 'пусто'], ['🖼️', 'волна.png', '1,2 МБ'], ['🎵', 'трек.flac', '8,4 МБ'], ['📄', 'заметки.txt', '2 КБ']]
        .map(f => `<div class="card row" style="cursor:pointer" onclick="toast('${f[1]} — открыто (демо)')"><span style="font-size:22px">${f[0]} <b>${f[1]}</b></span><small style="opacity:.7">${f[2]}</small></div>`).join(''); },

    terminal(el) { el.innerHTML = `<div id="term-out">XrossOS terminal · введите help\n$ </div>
      <input id="term-in" placeholder="команда… (help, neofetch, ls, date, open music, clear, exit)" autofocus>`;
      const out = el.querySelector('#term-out'), inp = el.querySelector('#term-in');
      const print = (s) => out.textContent += s + '\n$ ';
      inp.onkeydown = (e) => { if (e.key !== 'Enter') return; const cmd = inp.value.trim(); inp.value = '';
        out.textContent += cmd + '\n';
        const c = cmd.toLowerCase();
        if (c === 'help') print('help · neofetch · ls · date · open <приложение> · clear · exit');
        else if (c === 'neofetch') print('guest@xross\nOS: XrossOS 0.1 Wave (Alpine Linux)\nKernel: 6.6 LTS · WM: Cage/Wayland\nShell: XMB · RAM shell: ~120 MB');
        else if (c === 'ls') print('Фото  Музыка  Загрузки  волна.png  трек.flac  заметки.txt');
        else if (c === 'date') print(new Date().toLocaleString('ru-RU'));
        else if (c === 'clear') out.textContent = '$ ';
        else if (c === 'exit') closeApp();
        else if (c.startsWith('open ')) { const a = XROSS_APPS.find(x => x.title.toLowerCase().includes(c.slice(5))); a ? openApp(a) : print('не найдено'); }
        else if (c) print('команда не найдена: ' + cmd);
        out.scrollTop = 1e6; }; },

    notes(el) { el.innerHTML = `<h2 style="margin-bottom:14px">📝 Заметки</h2>
      <textarea id="nt" rows="12" placeholder="Пишите… сохраняется автоматически">${localStorage.xross_notes || ''}</textarea>`;
      el.querySelector('#nt').oninput = (e) => localStorage.xross_notes = e.target.value; },
  };

  /* ── Часы, тема, старт ─────────────────────── */
  const tick = () => $('#clock').textContent = new Date().toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' });
  tick(); setInterval(tick, 10000);
  if (localStorage.xross_hue) document.documentElement.style.setProperty('--hue', localStorage.xross_hue);
  render();
  setTimeout(() => toast('Добро пожаловать в XrossOS! Стрелки — навигация, V — голос 🎤'), 600);
  return { move, select, back, openApp, closeApp };
})();
