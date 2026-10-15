// Run in the head so the resolved theme is applied before the first paint.
(() => {
  'use strict';
  const root = document.documentElement;
  const media = window.matchMedia('(prefers-color-scheme: dark)');
  const valid = value => ['system', 'light', 'dark'].includes(value);
  let preference = 'system';
  try {
    const saved = localStorage.getItem('room-controller-theme');
    if (valid(saved)) preference = saved;
  } catch (_) { /* Storage can be unavailable on a local/private page. */ }

  function apply() {
    const theme = preference === 'system' ? (media.matches ? 'dark' : 'light') : preference;
    root.dataset.theme = theme;
    root.style.colorScheme = theme;
    document.querySelector('meta[name="theme-color"]').content = theme === 'dark' ? '#101d25' : '#edf3f4';
    document.dispatchEvent(new CustomEvent('room-theme-change', {detail:theme}));
  }
  apply();
  const followSystem = () => { if (preference === 'system') apply(); };
  if (media.addEventListener) media.addEventListener('change', followSystem);
  else media.addListener(followSystem);
  document.addEventListener('DOMContentLoaded', () => {
    const control = document.getElementById('theme-preference');
    control.value = preference;
    control.addEventListener('change', () => {
      preference = valid(control.value) ? control.value : 'system';
      try { localStorage.setItem('room-controller-theme', preference); } catch (_) {}
      apply();
    });
  }, {once:true});
})();
