"use strict";
/* Alert sounds. They are made with the browser's Web Audio (no sound files to upload). Every sound is a short pattern of beeps:
   what you hear is the same on every device, only the volume of the speaker changes.

   Browsers keep a page silent until the person has clicked or tapped on it once. So the page asks for one tap ("Turn on alert sounds");
   after that, alerts can make noise as long as the page stays open (also in a background tab). A closed page cannot play anything:
   for that use the Blynk app's notification (see README.txt, "Alerts when the page is closed"). */
const Sound = (() => {
  const saved = (() => { try { return JSON.parse(store.get('jh_sound') || '{}'); } catch (e) { return {}; } })();
  const st = { on: saved.on !== false, vol: typeof saved.vol === 'number' ? saved.vol : 0.8 };
  let ctx = null, master = null, lastEnd = 0;

  function ensure() {
    if (!ctx) {
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return null;
      ctx = new AC(); master = ctx.createGain(); master.connect(ctx.destination);
    }
    master.gain.value = Math.max(0, Math.min(1, st.vol));
    return ctx;
  }
  function unlock() {                                           // call from a click / tap
    const c = ensure(); if (!c) return 'none';
    if (c.state === 'suspended') c.resume().catch(() => {});
    return c.state;
  }
  const state = () => (ctx ? ctx.state : 'off');                // 'running' = alerts can be heard
  const ready = () => !!ctx && ctx.state === 'running';

  /* one beep at time t: frequency f (gliding to f1), length d seconds, loudness g, waveform type */
  function beep(t, f, d, type, g, f1) {
    const o = ctx.createOscillator(), a = ctx.createGain();
    o.type = type; o.frequency.setValueAtTime(f, t);
    if (f1) o.frequency.linearRampToValueAtTime(f1, t + d);
    a.gain.setValueAtTime(0.0001, t);
    a.gain.exponentialRampToValueAtTime(g, t + 0.012);
    a.gain.setValueAtTime(g, t + Math.max(0.02, d - 0.04));
    a.gain.exponentialRampToValueAtTime(0.0001, t + d);
    o.connect(a); a.connect(master); o.start(t); o.stop(t + d + 0.03);
  }
  /* a soft bell: a sine with a quick decay (for chimes) */
  function bell(t, f, d, g) {
    const o = ctx.createOscillator(), o2 = ctx.createOscillator(), a = ctx.createGain();
    o.type = 'sine'; o.frequency.value = f; o2.type = 'sine'; o2.frequency.value = f * 2.01;
    a.gain.setValueAtTime(0.0001, t); a.gain.exponentialRampToValueAtTime(g, t + 0.01); a.gain.exponentialRampToValueAtTime(0.0001, t + d);
    o.connect(a); o2.connect(a); a.connect(master); o.start(t); o2.start(t); o.stop(t + d + 0.05); o2.stop(t + d + 0.05);
  }

  /* each pattern schedules its beeps from time t and returns how long it lasts */
  const PATTERNS = {
    siren:   { label: 'Siren',            note: 'rising and falling, 3 s',      run(t) { let x = t; for (let i = 0; i < 8; i++) { beep(x, i % 2 ? 700 : 980, 0.4, 'square', 0.5, i % 2 ? 980 : 700); x += 0.4; } return x - t; } },
    alarm:   { label: 'Alarm',            note: 'fast beeps in groups',          run(t) { let x = t; for (let g = 0; g < 3; g++) { for (let i = 0; i < 4; i++) { beep(x, 1400, 0.12, 'square', 0.45); x += 0.2; } x += 0.25; } return x - t; } },
    urgent:  { label: 'Urgent',           note: 'three rising beeps, twice',     run(t) { let x = t; for (let r = 0; r < 2; r++) { [1000, 1250, 1500].forEach(f => { beep(x, f, 0.16, 'square', 0.45); x += 0.22; }); x += 0.3; } return x - t; } },
    beeps3:  { label: 'Three beeps',      note: 'three short beeps',             run(t) { let x = t; for (let i = 0; i < 3; i++) { beep(x, 1000, 0.18, 'triangle', 0.65); x += 0.3; } return x - t; } },
    twotone: { label: 'Two tones',        note: 'ding-dong, twice',              run(t) { let x = t; for (let r = 0; r < 2; r++) { beep(x, 660, 0.28, 'triangle', 0.65); beep(x + 0.3, 880, 0.4, 'triangle', 0.65); x += 0.95; } return x - t; } },
    descend: { label: 'Falling tones',    note: 'three tones going down',        run(t) { let x = t; [880, 700, 520].forEach(f => { beep(x, f, 0.28, 'triangle', 0.6); x += 0.34; }); return x - t; } },
    soft:    { label: 'Soft blip',        note: 'two quiet blips',               run(t) { bell(t, 784, 0.35, 0.25); bell(t + 0.28, 659, 0.45, 0.25); return 0.8; } },
    chime:   { label: 'Chime',            note: 'a pleasant three note chime',   run(t) { [523, 659, 784].forEach((f, i) => bell(t + i * 0.17, f, 0.7, 0.28)); return 1.1; } },
    ding:    { label: 'Ding',             note: 'a bright bell',                 run(t) { bell(t, 1318, 0.9, 0.3); bell(t + 0.12, 1760, 0.8, 0.2); return 1.1; } },
    none:    { label: 'Silent',           note: 'no sound',                      run() { return 0; } }
  };

  function play(name) {                                         // returns the length in seconds, or 0 when nothing could be played
    const p = PATTERNS[name];
    if (!p || name === 'none' || !st.on) return 0;
    const c = ensure(); if (!c) return 0;
    if (c.state !== 'running') { if (c.state === 'suspended') c.resume().catch(() => {}); return 0; }
    const t = Math.max(c.currentTime + 0.03, lastEnd + 0.15);   // sounds queue one after the other instead of mixing
    const d = p.run(t); lastEnd = t + d;
    return d;
  }
  function test(name) { const was = st.on; st.on = true; unlock(); const d = play(name); st.on = was; return d; }

  function set(key, v) { st[key] = v; store.set('jh_sound', JSON.stringify(st)); if (master) master.gain.value = st.vol; }
  // the first click or tap anywhere turns the audio on (the browser's rule), so the next alert is audible
  ['pointerdown', 'keydown', 'touchend'].forEach(ev => document.addEventListener(ev, () => { if (st.on) unlock(); }, { passive: true }));
  return { st, PATTERNS, play, test, unlock, state, ready, set, tap: () => { const c = ensure(); if (!c) return null; const a = c.createAnalyser(); a.fftSize = 2048; master.connect(a); return a; } };   // tap(): an analyser on the output (used to check that the sounds really make a signal)
})();
