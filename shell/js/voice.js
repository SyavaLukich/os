/* Голосовое управление: Web Speech API (ru-RU), полностью офлайн-совместимые
   команды навигации. В образе ОС распознавание идёт через Cog/Chromium
   либо локальный Vosk (см. docs/architecture.md). Клавиша V / клик по 🎤. */
window.Voice = (() => {
  const SR = window.SpeechRecognition || window.webkitSpeechRecognition;
  const bar = document.getElementById('voicebar');
  const txt = document.getElementById('voice-text');
  const mic = document.getElementById('mic-indicator');
  let rec = null, on = false;

  const say = (s) => {
    toast('🔊 ' + s);
    if (localStorage.xross_tts !== '0' && 'speechSynthesis' in window) {
      speechSynthesis.cancel();
      const u = new SpeechSynthesisUtterance(s); u.lang = 'ru-RU'; u.rate = 1.05;
      speechSynthesis.speak(u);
    }
  };

  function handle(raw) {
    const cmd = raw.toLowerCase().trim();
    txt.textContent = '«' + raw + '»';
    const X = window.XMB;
    if (/вправо|дальше|след/.test(cmd)) return X.move(1, 0);
    if (/влево|назад по меню|предыд/.test(cmd) && !/закр/.test(cmd)) return X.move(-1, 0);
    if (/вниз/.test(cmd)) return X.move(0, 1);
    if (/вверх/.test(cmd)) return X.move(0, -1);
    if (/выбери|открой$|запусти|включи/.test(cmd) && !/открой\s+\S/.test(cmd)) return X.select();
    if (/домой|главн|закрой|выйди|стоп/.test(cmd)) return X.back(), say('Главный экран');
    if (/полн.*экран|во весь/.test(cmd)) return document.documentElement.requestFullscreen?.(), say('Полный экран');
    if (/время|который час/.test(cmd))
      return say('Сейчас ' + new Date().toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' }));
    if (/привет|здравствуй/.test(cmd)) return say('Привет! Скажите: открой настройки, или просто — вниз, выбери.');
    const m = cmd.match(/(?:открой|запусти|включи)\s+(.+)/);
    if (m) {
      const app = XROSS_APPS.find(a => a.title.toLowerCase().includes(m[1].trim()) ||
                                       m[1].trim().includes(a.title.toLowerCase().split(' ')[0]));
      if (app) return X.openApp(app), say('Открываю: ' + app.title);
      return say('Не нашла приложение ' + m[1]);
    }
    if (/помощь|команды|что ты умеешь/.test(cmd))
      return say('Команды: вправо, вниз, выбери, открой музыку, домой, который час.');
    say('Не поняла. Скажите «помощь».');
  }

  function toggle() {
    if (!SR) return say('Браузер без распознавания речи. В образе ОС используйте Vosk — см. документацию.'), bar.classList.add('show'),
      txt.textContent = 'Распознавание недоступно в этом браузере (нужен Chrome/Edge или образ XrossOS с Vosk)';
    if (on) return rec.stop();
    rec = new SR();
    rec.lang = 'ru-RU'; rec.continuous = true; rec.interimResults = true;
    rec.onresult = (e) => {
      const r = e.results[e.results.length - 1];
      txt.textContent = r[0].transcript;
      if (r.isFinal) handle(r[0].transcript);
    };
    rec.onstart = () => { on = true; mic.classList.add('listening'); bar.classList.add('show'); txt.textContent = 'Слушаю… скажите «помощь»'; };
    rec.onend = () => { on = false; mic.classList.remove('listening'); bar.classList.remove('show'); };
    rec.onerror = (e) => { if (e.error !== 'aborted') say('Ошибка микрофона: ' + e.error); };
    try { rec.start(); } catch (_) {}
  }

  addEventListener('keydown', (e) => { if (e.key.toLowerCase() === 'v' || e.key.toLowerCase() === 'м') toggle(); });
  mic.addEventListener('click', toggle);
  return { toggle, say, get on() { return on; } };
})();
