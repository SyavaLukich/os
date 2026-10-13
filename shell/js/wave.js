/* Фирменная «волна» PS3: плавная светящаяся лента + блёстки на Canvas.
   Один requestAnimationFrame, без библиотек — не грузит слабую ОС. */
(function () {
  const cv = document.getElementById('wave');
  const ctx = cv.getContext('2d');
  let W, H, t = 0, sparks = [];
  const reduced = () => document.body.classList.contains('reduce-motion');

  function resize() {
    W = cv.width = innerWidth; H = cv.height = innerHeight;
    sparks = Array.from({ length: 70 }, () => ({
      x: Math.random() * W, y: Math.random() * H,
      r: Math.random() * 1.8 + .4, s: Math.random() * .35 + .08, o: Math.random() * .7 + .15
    }));
  }
  addEventListener('resize', resize); resize();

  function ribbon(color, amp, len, speed, yBase, width) {
    ctx.beginPath();
    for (let x = 0; x <= W; x += 6) {
      const y = yBase + Math.sin(x / len + t * speed) * amp
                      + Math.sin(x / (len * .55) - t * speed * .7) * amp * .45;
      x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.strokeStyle = color; ctx.lineWidth = width;
    ctx.shadowColor = color; ctx.shadowBlur = 24; ctx.stroke(); ctx.shadowBlur = 0;
  }

  (function frame() {
    requestAnimationFrame(frame);
    if (!reduced()) t += 0.012;
    const hue = getComputedStyle(document.documentElement).getPropertyValue('--hue') || 222;
    const g = ctx.createLinearGradient(0, 0, 0, H);
    g.addColorStop(0, `hsl(${hue} 65% 10%)`);
    g.addColorStop(.55, `hsl(${+hue + 25} 70% 16%)`);
    g.addColorStop(1, `hsl(${+hue + 55} 75% 7%)`);
    ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);

    ribbon(`hsla(${hue},95%,72%,.85)`, 42, 340, 1.0, H * .46, 2.2);
    ribbon(`hsla(${+hue + 40},95%,65%,.45)`, 58, 260, .7, H * .50, 5);
    ribbon(`hsla(${+hue + 80},90%,60%,.22)`, 75, 200, .5, H * .54, 12);

    ctx.fillStyle = '#fff';
    for (const s of sparks) {
      s.y -= s.s; if (s.y < -4) { s.y = H + 4; s.x = Math.random() * W; }
      ctx.globalAlpha = s.o * (0.6 + 0.4 * Math.sin(t * 2 + s.x));
      ctx.beginPath(); ctx.arc(s.x, s.y, s.r, 0, 7); ctx.fill();
    }
    ctx.globalAlpha = 1;
  })();
})();
