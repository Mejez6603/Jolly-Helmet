"use strict";
/* The tab bar: Monitor, Alerts, Reports. The address says which one is open (index.html#alerts ...), so a tab can be
   bookmarked or sent to somebody. Every tab draws itself when it is opened. */
const Tabs = (() => {
  const TABS = [
    { id: 'monitor',   sec: 'monitorTab', btn: 'tabMon',    title: 'Jolly Helmet Online Monitor', show() {} },
    { id: 'alerts',    sec: 'alertsTab',  btn: 'tabAlerts', title: 'Jolly Helmet Alerts',         show() { Alerts.drawTab(); } },
    { id: 'reports',   sec: 'reportsTab', btn: 'tabRep',    title: 'Jolly Helmet Reports',        show() { Reports.refresh(); } }
  ];
  function route() {
    const h = location.hash.replace(/^#/, '').split('/')[0], cur = TABS.find(t => t.id === h) || TABS[0];
    TABS.forEach(t => { const on = t === cur; $(t.sec).hidden = !on; $(t.btn).classList.toggle('on', on); if (on) $(t.btn).setAttribute('aria-current', 'page'); else $(t.btn).removeAttribute('aria-current'); });
    document.body.dataset.tab = cur.id;
    document.title = cur.title;
    if (typeof Alerts !== 'undefined') Alerts.refreshTitle();
    document.querySelectorAll('.noconn').forEach(el => { el.hidden = !!(cfg.token || DEMO); });
    cur.show();
    window.scrollTo(0, 0);
  }
  window.addEventListener('hashchange', route);
  return { route };
})();
