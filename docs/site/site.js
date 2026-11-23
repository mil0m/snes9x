// Fills the page's pictures from media.json (written by tools/site/build.sh), plays clips
// only while they're on screen, and wires the hero's divider and the cards' hold-to-compare.
(async () => {
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let media = {};
  try { media = await (await fetch('media.json')).json(); } catch { /* no pictures built */ }

  const el = (tag, props = {}, kids = []) => {
    const n = Object.assign(document.createElement(tag), props);
    for (const k of kids) n.append(k);
    return n;
  };
  const video = (src, poster, extra = {}) => el('video', {
    src, poster, muted: true, loop: true, playsInline: true, preload: 'none', ...extra,
  });
  const missing = (what) => el('div', { className: 'missing', textContent: `${what} isn't built yet: run tools/site/build.sh.` });

  // Clips play only while visible (and never by themselves with reduced motion).
  const seen = new IntersectionObserver((entries) => {
    for (const e of entries) {
      for (const v of e.target.querySelectorAll('video')) {
        if (e.isIntersecting && !still) v.play().catch(() => {});
        else v.pause();
      }
    }
  }, { threshold: 0.35 });

  // Two videos of the same frames: keep the clean one on the corrupted one's time.
  const pair = (dirty, clean) => {
    const sync = () => { if (Math.abs(clean.currentTime - dirty.currentTime) > 0.05) clean.currentTime = dirty.currentTime; };
    dirty.addEventListener('play', () => { clean.play().catch(() => {}); sync(); });
    dirty.addEventListener('pause', () => clean.pause());
    dirty.addEventListener('seeked', sync);
    setInterval(() => { if (!dirty.paused) sync(); }, 1000);
  };

  // The hero.
  const hero = document.querySelector('[data-compare]');
  if (hero) {
    const m = media.hero;
    const screen = hero.querySelector('.screen');
    if (!m) {
      screen.replaceWith(missing('The hero clip'));
      hero.querySelector('.slider').hidden = true;
    } else {
      const [clean, dirty] = hero.querySelectorAll('video');
      clean.preload = dirty.preload = 'auto';
      pair(dirty, clean);
      seen.observe(screen);
      const range = hero.querySelector('input');
      const set = () => screen.style.setProperty('--split', `${range.value}%`);
      range.addEventListener('input', set);
      set();
      hero.querySelector('figcaption').textContent = `${m.caption}. Same buttons both times: drag to compare.`;
    }
  }

  // A card's clip: corrupted, with the clean run under it while the button is held.
  for (const fig of document.querySelectorAll('[data-clip]')) {
    const m = media[fig.dataset.clip];
    if (!m) { fig.append(missing('This clip')); continue; }
    const dirty = video(`media/${m.name}.mp4`, `media/${m.name}.jpg`, { ariaLabel: `${m.caption}, corrupted` });
    const clean = video(`media/${m.name}-clean.mp4`, `media/${m.name}.jpg`, { className: 'clean-layer', ariaHidden: 'true' });
    const screen = el('div', { className: 'screen' }, [dirty, clean]);
    const hold = el('button', { type: 'button', className: 'hold', textContent: 'Hold to see it clean' });
    hold.setAttribute('aria-pressed', 'false');
    const show = (on) => {
      screen.classList.toggle('showing-clean', on);
      hold.setAttribute('aria-pressed', String(on));
      hold.textContent = on ? 'Clean' : 'Hold to see it clean';
    };
    hold.addEventListener('pointerdown', (e) => { hold.setPointerCapture(e.pointerId); show(true); });
    for (const t of ['pointerup', 'pointercancel', 'lostpointercapture']) hold.addEventListener(t, () => show(false));
    hold.addEventListener('keydown', (e) => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); show(true); } });
    hold.addEventListener('keyup', () => show(false));
    hold.addEventListener('blur', () => show(false));
    if (still) {
      const play = el('button', { type: 'button', className: 'hold', textContent: 'Play' });
      play.style.left = 'auto'; play.style.right = '12px';
      play.addEventListener('click', () => (dirty.paused ? dirty.play() : dirty.pause()));
      screen.append(play);
    }
    screen.append(hold);
    pair(dirty, clean);
    seen.observe(screen);
    fig.append(screen, el('figcaption', { textContent: m.caption }));
  }

  // A card's window: a screenshot of the app.
  for (const fig of document.querySelectorAll('[data-shot]')) {
    const m = media[fig.dataset.shot];
    if (!m) { fig.append(missing('This picture')); continue; }
    const img = el('img', { src: `media/${m.name}.png`, alt: m.caption, loading: 'lazy' });
    // The tour takes its pictures at 2x: show them at the size the window really is.
    img.addEventListener('load', () => { img.style.maxWidth = `${img.naturalWidth / 2}px`; });
    fig.append(el('div', { className: 'window' }, [img]), el('figcaption', { textContent: m.caption }));
  }

  // The gallery: plays while pointed at or focused.
  for (const fig of document.querySelectorAll('[data-mini]')) {
    const m = media[fig.dataset.mini];
    if (!m) { fig.remove(); continue; }
    const v = video(`media/${m.name}.mp4`, `media/${m.name}.jpg`, { ariaLabel: m.caption });
    const screen = el('div', { className: 'screen', tabIndex: 0 }, [v]);
    const on = () => v.play().catch(() => {});
    const off = () => v.pause();
    screen.addEventListener('pointerenter', on);
    screen.addEventListener('pointerleave', off);
    screen.addEventListener('focus', on);
    screen.addEventListener('blur', off);
    screen.addEventListener('click', () => (v.paused ? on() : off()));
    const [title, ...rest] = m.caption.split(', ');
    fig.append(screen, el('figcaption', {}, [title, el('span', { textContent: rest.join(', ') })]));
  }
})();
