/* XrossOS — реестр приложений. В реальной ОС сюда подмешиваются
   манифесты из /usr/share/xross/apps/*.json (см. os/overlay). */
window.XROSS_APPS = [
  // ── Пользователи ──────────────────────────────
  { id:'profile', cat:'users', title:'Профиль игрока', sub:'Гость · Уровень 1', icon:'👤', color:'#7c5cff', builtin:'profile' },
  { id:'trophies', cat:'users', title:'Достижения', sub:'3 из 42 получено', icon:'🏆', color:'#f5b301', builtin:'trophies' },

  // ── Настройки ─────────────────────────────────
  { id:'settings', cat:'settings', title:'Настройки системы', sub:'Тема, звук, голос, анимации', icon:'⚙️', color:'#9aa7b4', builtin:'settings' },
  { id:'voice-settings', cat:'settings', title:'Голосовое управление', sub:'Распознавание речи (русский)', icon:'🎤', color:'#ff5c7a', builtin:'voice' },
  { id:'about', cat:'settings', title:'О системе XrossOS', sub:'Версия 0.1 «Wave»', icon:'ℹ️', color:'#4cc9f0', builtin:'about' },

  // ── Фото / Музыка / Видео ─────────────────────
  { id:'photos', cat:'photo', title:'Фотографии', sub:'Галерея снимков', icon:'🖼️', color:'#38d39f', builtin:'photos' },
  { id:'music', cat:'music', title:'Музыка', sub:'3 трека · синтезатор WebAudio', icon:'🎵', color:'#ff7ac6', builtin:'music' },
  { id:'video', cat:'video', title:'Видео', sub:'Демо-плеер', icon:'🎬', color:'#ff8a5c', builtin:'video' },

  // ── Игры ──────────────────────────────────────
  { id:'bubbles', cat:'game', title:'Пузыри', sub:'Мини-игра в стиле PS Vita', icon:'🫧', color:'#4cc9f0', builtin:'bubbles' },
  { id:'snake', cat:'game', title:'Волна-змейка', sub:'Классика на канвасе', icon:'🐍', color:'#38d39f', builtin:'snake' },

  // ── Сеть / Приложения ─────────────────────────
  { id:'browser', cat:'network', title:'Браузер', sub:'Открывается на весь экран', icon:'🌐', color:'#4cc9f0', builtin:'browser' },
  { id:'store', cat:'network', title:'Магазин приложений', sub:'Установка в один клик', icon:'🛍️', color:'#f5b301', builtin:'store' },
  { id:'files', cat:'apps', title:'Файлы', sub:'Домашний каталог', icon:'📁', color:'#ffd166', builtin:'files' },
  { id:'terminal', cat:'apps', title:'Терминал', sub:'help — список команд', icon:'>_', color:'#a7f3d0', builtin:'terminal' },
  { id:'notes', cat:'apps', title:'Заметки', sub:'Сохраняются локально', icon:'📝', color:'#fef08a', builtin:'notes' },
];

window.XROSS_CATEGORIES = [
  { id:'users',    title:'Пользователи', icon:'👤' },
  { id:'settings', title:'Настройки',    icon:'⚙️' },
  { id:'photo',    title:'Фото',         icon:'🖼️' },
  { id:'music',    title:'Музыка',       icon:'🎵' },
  { id:'video',    title:'Видео',        icon:'🎬' },
  { id:'game',     title:'Игры',         icon:'🎮' },
  { id:'network',  title:'Сеть',         icon:'🌐' },
  { id:'apps',     title:'Приложения',   icon:'▦' },
];
