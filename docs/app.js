/* =========================================================
   GoaSynth site — interactions
   No dependencies, no network requests, no tracking.
   ========================================================= */
(function () {
  'use strict';

  /* ---------------------------------------------------------
     CONFIG — the only thing to edit when you know your URLs.
     See Fulfil/CHECKOUT.md for the full store setup playbook.

     checkout     Hosted checkout URL from your merchant of
                  record. Every Buy button opens this.
                    Lemon Squeezy  https://<store>.lemonsqueezy.com/buy/<uuid>
                    Gumroad        https://<name>.gumroad.com/l/<slug>
                    Paddle         the checkout link Paddle copies for you
                    FastSpring     the storefront URL it gives you
     store        Store name, dropped into the "handled by …" copy.
     vatIncluded  true when your store price is tax-inclusive, so the
                  pricing card says "VAT included"; false when the
                  store adds VAT on top, so it says "VAT added at
                  checkout" instead. Keep it in step with the store.
     ordersEmail  Fallback while the store isn't live yet: every Buy
                  button becomes a pre-filled order email.
     support      Support/contact address; falls back to ordersEmail.
     download     Your store's own customer/library page, so buyers
                  can re-download. Left empty until the store has one:
                  the Install buttons then go to the steps on this page,
                  which say the link arrives with the receipt. It is
                  deliberately NOT <repo>/releases - the repository is
                  public, so the paid build never goes there, and an
                  empty releases page is worse than instructions.
     repo         Public source repository. Derived from a *.github.io
                  hostname when it is left empty, so a fork points at
                  itself without editing anything.
     machineIdField
                  true when the store's checkout collects the buyer's
                  MACHINE ID in a custom field, which is what the serial is
                  signed against (see Fulfil/CHECKOUT.md §3). Set false if you take
                  the machine id over email instead — the note on the pricing
                  card hides itself rather than promising a field that is
                  not there.
     --------------------------------------------------------- */
  var CONFIG = {
    price: '€15',        // shown wherever the markup has <span data-price>
                         // keep this in step with the price on the legal pages
    store: 'Lemon Squeezy', // the merchant of record, as the legal pages name it
    vatIncluded: true,   // set false if your store adds VAT on top
    checkout: '',        // TODO: paste the Lemon Squeezy buy link when the product
                         // is live — https://<store>.lemonsqueezy.com/buy/<uuid>
                         // (Fulfil/CHECKOUT.md §2–3)
    ordersEmail: 'goasynth.orders@gmail.com',
    support: 'goasynth.support@gmail.com',
    download: '',        // TODO: the store's customer-library URL, once it exists
    repo: 'https://github.com/Y4m4/GoaSynth',
    machineIdField: false // no checkout yet: the machine ID arrives by email
                          // (the order template asks for it). Flip to true when
                          // the Lemon Squeezy custom field is in place
  };

  var ORDER_SUBJECT = 'GoaSynth licence';
  var ORDER_BODY =
    'I would like to buy GoaSynth (one personal licence).\n\n' +
    'Name:\nCountry (for VAT):\nEmail for the receipt:\nOS: Windows / macOS / Linux\n' +
    '\nMachine ID (shown on the plugin activation screen):\n';

  function mailtoOrder() {
    return 'mailto:' + CONFIG.ordersEmail +
      '?subject=' + encodeURIComponent(ORDER_SUBJECT + ' \u2014 ' + CONFIG.price) +
      '&body=' + encodeURIComponent(ORDER_BODY);
  }

  function deriveFromHost() {
    var host = location.hostname;

    // The last ten characters of "y4m4.github.io" are ".github.io", dot
    // included — comparing them to "github.io" is never true, so this used to
    // return before deriving anything and every *.github.io deployment kept an
    // empty repo URL. (It hid the Source links rather than breaking a page,
    // which is why it went unnoticed.)
    if (host.slice(-10) !== '.github.io') return;
    var parts = location.pathname.split('/').filter(Boolean);
    var user = host.replace('.github.io', '');
    if (!user || !parts.length) return;
    var base = 'https://github.com/' + user + '/' + parts[0];
    CONFIG.repo = CONFIG.repo || base;
  }

  var pageUrl = location.href.split('#')[0];

  function external(a, href) {
    a.href = href;
    if (/^https?:/i.test(href)) { a.target = '_blank'; a.rel = 'noopener'; }
    else { a.removeAttribute('target'); a.removeAttribute('rel'); }
  }

  function resolveLinks() {
    deriveFromHost();

    // CONFIG.download is never derived, and never filled in with a fragment.
    // Deriving it from the repository's releases page is how this used to work,
    // and it is wrong twice over: the repository is public, so the paid build
    // must never be published there, and a link to an empty releases page is a
    // worse answer than the install steps below. The copy-link button reads
    // CONFIG.download directly too, so putting '#install' in here would have it
    // copy the string "#install" and call that a download link.

    // price is written once and stamped everywhere
    [].forEach.call(document.querySelectorAll('[data-price]'), function (el) {
      el.textContent = CONFIG.price;
    });

    // …and the tax wording follows however the store is configured
    [].forEach.call(document.querySelectorAll('[data-price-note]'), function (el) {
      el.textContent = CONFIG.vatIncluded
        ? 'one-time · VAT included where applicable'
        : 'one-time · VAT added at checkout';
    });

    if (CONFIG.checkout && !/^https?:\/\//i.test(CONFIG.checkout)) {
      console.warn('GoaSynth: CONFIG.checkout should be a full https:// checkout URL —', CONFIG.checkout);
    }

    // One mode at a time: a live store, order-by-email, or neither. Any block
    // tagged [data-when] is shown only in its own mode, so the page never
    // advertises a payment path that doesn't exist yet.
    var mode = CONFIG.checkout ? 'checkout' : (CONFIG.ordersEmail ? 'email' : 'none');
    [].forEach.call(document.querySelectorAll('[data-when]'), function (el) {
      el.hidden = el.getAttribute('data-when') !== mode;
    });
    [].forEach.call(document.querySelectorAll('[data-store-name]'), function (el) {
      el.textContent = CONFIG.store || 'our checkout provider';
    });

    // The serial is signed for the buyer's machine id, so the page only
    // promises a checkout field for it when the store actually has one.
    [].forEach.call(document.querySelectorAll('[data-when-machineid]'), function (el) {
      el.hidden = !CONFIG.machineIdField;
    });

    // Source links: shown only once there is a repository to point at. The old
    // fallback was the current page, which made "Source" link to the page the
    // visitor was already reading.
    [].forEach.call(document.querySelectorAll('[data-link="repo"]'), function (a) {
      if (CONFIG.repo) {
        a.hidden = false;
        external(a, CONFIG.repo);
      } else {
        a.hidden = true;
      }
    });
    [].forEach.call(document.querySelectorAll('[data-when-repo]'), function (el) {
      el.hidden = !CONFIG.repo;
    });

    [].forEach.call(document.querySelectorAll('[data-link="download"]'), function (a) {
      // no customer link yet → send people to the install steps, which explain
      // that the download page arrives with the purchase email.
      external(a, CONFIG.download || '#install');
    });

    // The install steps carry a "Copy link" button for the download URL. With no
    // download configured there is no link to copy, so the button is hidden
    // rather than copying this page's URL and calling that a download link;
    // [data-until-download] blocks explain where the build comes from instead.
    [].forEach.call(document.querySelectorAll('[data-copy-target]'), function (btn) {
      btn.hidden = !CONFIG.download;
    });
    [].forEach.call(document.querySelectorAll('[data-until-download]'), function (el) {
      el.hidden = !!CONFIG.download;
    });

    // Buy buttons: hosted checkout first, then a pre-filled order email,
    // then (before either exists) the on-page pricing section.
    var buyHref = CONFIG.checkout
      ? CONFIG.checkout
      : (CONFIG.ordersEmail ? mailtoOrder() : '#buy');

    [].forEach.call(document.querySelectorAll('[data-link="buy"]'), function (a) {
      external(a, buyHref);
      if (!CONFIG.checkout && !CONFIG.ordersEmail) {
        a.setAttribute('title', 'Checkout link not configured yet — see CONFIG in app.js');
      }
    });

    // Contact links: mailto when an address exists, otherwise the whole
    // "ask first" sentence is removed rather than pointing nowhere.
    var contact = CONFIG.support || CONFIG.ordersEmail;
    [].forEach.call(document.querySelectorAll('[data-contact-wrap]'), function (el) {
      if (!contact) el.hidden = true;
    });
    [].forEach.call(document.querySelectorAll('[data-link="contact"]'), function (a) {
      if (!contact) return;
      a.href = 'mailto:' + contact;
      a.removeAttribute('target');
    });
  }

  /* ---------- sticky bar + active nav section ---------- */
  function initTopbar() {
    var bar = document.getElementById('topbar');
    var nav = document.getElementById('nav');
    var burger = document.getElementById('burger');
    var links = [].slice.call(nav.querySelectorAll('a'));

    function onScroll() {
      bar.classList.toggle('is-stuck', window.scrollY > 8);

      var current = '';
      links.forEach(function (a) {
        var section = document.getElementById(a.getAttribute('href'));
        if (!section) return;
        if (section.getBoundingClientRect().top <= 140) current = a.getAttribute('href');
      });
      links.forEach(function (a) {
        a.classList.toggle('is-active', a.getAttribute('href') === current);
      });
    }
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();

    burger.addEventListener('click', function () {
      var open = nav.classList.toggle('is-open');
      burger.setAttribute('aria-expanded', open ? 'true' : 'false');
    });
    links.forEach(function (a) {
      a.addEventListener('click', function () {
        nav.classList.remove('is-open');
        burger.setAttribute('aria-expanded', 'false');
      });
    });
  }

  /* ---------- scroll reveal ---------- */
  function initReveal() {
    var targets = document.querySelectorAll(
      '.card, .panel, .deep, .preset, .theme-card, .shot, .hero-shot, .matrix, .chain, .faq details, .spec-table > div, .build, .callout, .price-card, .buy-side > .panel'
    );
    if (!('IntersectionObserver' in window)) return;

    var fired = false;
    var io = new IntersectionObserver(function (entries) {
      fired = true;
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        entry.target.classList.add('is-in');
        io.unobserve(entry.target);
      });
    }, { rootMargin: '0px 0px -8% 0px', threshold: .08 });

    var pending = [].filter.call(targets, function (el) { return !el.closest('.lightbox'); });
    pending.forEach(function (el, i) {
      el.classList.add('reveal');
      el.style.transitionDelay = (Math.min(i % 6, 5) * 45) + 'ms';
      io.observe(el);
    });

    // Safety net: if the observer never reports (hidden/throttled tab, exotic
    // embedding), never leave the page invisible.
    setTimeout(function () {
      if (fired) return;
      pending.forEach(function (el) { el.classList.add('is-in'); });
    }, 1500);
  }

  /* ---------- screenshot lightbox ---------- */
  function initLightbox() {
    var box = document.getElementById('lightbox');
    var img = document.getElementById('lightboxImg');
    var cap = document.getElementById('lightboxCap');
    var close = document.getElementById('lightboxClose');

    function open(src, alt, text) {
      img.src = src; img.alt = alt || ''; cap.textContent = text || '';
      box.hidden = false;
      document.body.style.overflow = 'hidden';
    }
    function hide() {
      box.hidden = true; img.src = '';
      document.body.style.overflow = '';
    }

    document.querySelectorAll('[data-lightbox]').forEach(function (fig) {
      var picture = fig.querySelector('img');
      var caption = fig.querySelector('figcaption');
      if (!picture) return;
      picture.addEventListener('click', function () {
        open(picture.getAttribute('src'), picture.getAttribute('alt'), caption ? caption.textContent.trim() : '');
      });
    });

    close.addEventListener('click', hide);
    box.addEventListener('click', function (e) { if (e.target === box) hide(); });
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && !box.hidden) hide(); });
  }

  /* ---------- toast ---------- */
  var toastEl, toastTimer;
  function toast(message) {
    if (!toastEl) toastEl = document.getElementById('toast');
    toastEl.textContent = message;
    toastEl.classList.add('is-on');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { toastEl.classList.remove('is-on'); }, 2200);
  }

  function copyText(text, message) {
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(function () { toast(message); },
        function () { fallbackCopy(text, message); });
    } else {
      fallbackCopy(text, message);
    }
  }
  function fallbackCopy(text, message) {
    var ta = document.createElement('textarea');
    ta.value = text; ta.setAttribute('readonly', '');
    ta.style.position = 'fixed'; ta.style.opacity = '0';
    document.body.appendChild(ta); ta.select();
    try { document.execCommand('copy'); toast(message); } catch (e) { toast('Copy manually: ' + text); }
    document.body.removeChild(ta);
  }

  /* ---------- copy buttons ---------- */
  function initCopy() {
    document.querySelectorAll('.copy').forEach(function (btn) {
      btn.addEventListener('click', function (e) {
        e.preventDefault();

        if (btn.hasAttribute('data-code-copy')) {
          var block = btn.closest('.code-block');
          var pre = block && block.querySelector('pre');
          if (pre) copyText(pre.innerText, 'Build commands copied');
          return;
        }
        if (btn.hasAttribute('data-copy')) {
          copyText(btn.getAttribute('data-copy'), 'Path copied to clipboard');
          return;
        }
        if (btn.hasAttribute('data-copy-target')) {
          var url = CONFIG.download || pageUrl;
          copyText(url, CONFIG.download ? 'Download link copied' : 'Page link copied \u2014 download links arrive with your receipt');
        }
      });
    });
  }

  /* ---------- install tabs ---------- */
  function initOsTabs() {
    var tabs = [].slice.call(document.querySelectorAll('.os-tab'));
    var panels = [].slice.call(document.querySelectorAll('.os-panel'));
    tabs.forEach(function (tab) {
      tab.addEventListener('click', function () {
        var os = tab.getAttribute('data-os');
        tabs.forEach(function (t) { t.classList.toggle('is-active', t === tab); });
        panels.forEach(function (p) { p.hidden = p.getAttribute('data-os-panel') !== os; });
      });
    });
  }

  /* ---------- skin picker ---------- */
  function initSkins() {
    var root = document.documentElement;
    var cards = [].slice.call(document.querySelectorAll('.theme-card'));

    function apply(skin, remember) {
      root.setAttribute('data-theme', skin);
      cards.forEach(function (c) { c.classList.toggle('is-active', c.getAttribute('data-skin') === skin); });
      if (remember) { try { localStorage.setItem('goasynth-skin', skin); } catch (e) {} }
    }

    var stored = null;
    try { stored = localStorage.getItem('goasynth-skin'); } catch (e) {}
    if (stored) apply(stored, false);

    cards.forEach(function (card) {
      card.addEventListener('click', function () { apply(card.getAttribute('data-skin'), true); });
    });
  }

  /* ---------- boot ---------- */
  resolveLinks();
  initTopbar();
  initReveal();
  initLightbox();
  initCopy();
  initOsTabs();
  initSkins();
})();

/* =========================================================
   Play section — interactive trancegate demo.
   Same idea as the plugin's strip: click steps, hear the chop.
   No samples: a two-osc detuned saw through a lowpass, gated
   per step, with a tempo-synced feedback delay. Everything is
   synthesised in the browser with the Web Audio API.
   ========================================================= */
(function () {
  'use strict';

  var grid = document.getElementById('demo-gate');
  if (!grid) return;

  /* Gate shape rows: UPLIFT (3-on/1-off), OFF-BEAT, ROLLER, 16THS.
     Category cells (cols 0/4/8/12, dashed) hold the shape name and
     re-apply its pattern - they don't gate. Row 5 is the arp sequence. */
  var ROWS = 5, COLS = 16;
  var SHAPES = [
    [1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0],   // UPLIFT
    [0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0],   // OFF-BEAT
    [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0],   // ROLLER
    [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]    // 16THS
  ];
  var NAMES = ['UPLIFT', 'OFF-BEAT', 'ROLLER', '16THS'];
  var CAT_COLS = [0, 4, 8, 12];
  // Arp seed: A minor pentatonic run, one semitone-offset per 16th.
  var ARP_SEED = [0, 3, 7, 10, 12, 10, 7, 3, 0, 3, 7, 10, 12, 10, 7, 3];
  // Live state: [row][col] 0/1 for gate rows, semitone offset for the arp row.
  var state = [];
  var cells = [];
  var playBtn = document.querySelector('[data-demo-play]');
  var bpmInput = document.querySelector('[data-demo-bpm]');
  var bpmVal = document.querySelector('[data-demo-bpm-val]');
  var arpInput = document.querySelector('[data-demo-arp]');

  function cellAt(r, c) { return cells[r * COLS + c]; }

  function isCategory(r, c) { return r < SHAPES.length && CAT_COLS.indexOf(c) >= 0; }

  function applyShape(r) {
    for (var c = 0; c < COLS; ++c)
      if (!isCategory(r, c)) setCell(r, c, SHAPES[r][c]);
  }

  function setCell(r, c, v) {
    state[r][c] = v;
    cellAt(r, c).setAttribute('data-on', v ? '1' : '0');
  }

  function buildGrid() {
    for (var r = 0; r < ROWS; ++r) {
      state.push([]);
      for (var c = 0; c < COLS; ++c) {
        var b = document.createElement('button');
        b.type = 'button';
        if (r < SHAPES.length && isCategory(r, c)) {
          b.className = 'demo-bcat demo-hcat';
          b.textContent = c === 0 ? NAMES[r] : '';
          b.setAttribute('aria-label', NAMES[r] + ' shape');
          b.addEventListener('click', makeShapeHandler(r));
          b.removeAttribute('data-on');
        } else {
          b.setAttribute('aria-label', 'step ' + (c + 1) + ', row ' + (r + 1));
          b.addEventListener('click', makeToggleHandler(r, c));
          b.className = r === ROWS - 1 ? 'demo-arow' : '';
        }
        grid.appendChild(b);
        cells.push(b);
        state[r].push(0);
      }
    }
    for (var s = 0; s < SHAPES.length; ++s) applyShape(s);
    for (var a = 0; a < COLS; ++a) setCell(ROWS - 1, a, ARP_SEED[a]);
  }

  function makeShapeHandler(r) {
    return function () { applyShape(r); };
  }

  function makeToggleHandler(r, c) {
    return function () {
      var v = state[r][c] ? 0 : 1;
      // The arp row stores semitone offsets: toggling off silences the step.
      setCell(r, c, r === ROWS - 1 ? (v ? ARP_SEED[c] : 0) : v);
    };
  }

  buildGrid();

  /* ---- audio ---- */
  var ctx = null, master = null, lp = null, dly = null, dlyFb = null, dlyWet = null;
  var playing = false, step = 0, nextTime = 0, timer = null;
  var queue = [];   // scheduled {t, col} for the playhead flash

  function ensureAudio() {
    if (ctx) return;
    var AC = window.AudioContext || window.webkitAudioContext;
    ctx = new AC();
    master = ctx.createGain(); master.gain.value = 0.2;
    lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 2400;
    dly = ctx.createDelay(1.0);
    dlyFb = ctx.createGain(); dlyFb.gain.value = 0.3;
    dlyWet = ctx.createGain(); dlyWet.gain.value = 0.22;
    lp.connect(master);
    lp.connect(dly); dly.connect(dlyFb); dlyFb.connect(dly);
    dly.connect(dlyWet); dlyWet.connect(master);
    master.connect(ctx.destination);
  }

  function bpm() { return parseInt(bpmInput.value, 10) || 138; }
  function stepDur() { return 60 / bpm() / 4; }

  function voice(t, freq, dur) {
    var o1 = ctx.createOscillator(), o2 = ctx.createOscillator(), g = ctx.createGain();
    o1.type = 'sawtooth'; o2.type = 'sawtooth';
    o1.frequency.value = freq; o2.frequency.value = freq * 1.007;
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(0.5, t + 0.004);
    g.gain.setTargetAtTime(0, t + dur * 0.85, 0.02);
    o1.connect(g); o2.connect(g); g.connect(lp);
    o1.start(t); o2.start(t);
    o1.stop(t + dur + 0.15); o2.stop(t + dur + 0.15);
  }

  function schedule() {
    var ahead = ctx.currentTime + 0.12;
    while (nextTime < ahead) {
      var c = step % COLS;
      var dur = stepDur() * 0.92;
      var anyGate = false;
      for (var r = 0; r < SHAPES.length; ++r)
        if (state[r][c]) { anyGate = true; break; }
      if (anyGate) {
        var arp = arpInput.checked;
        var semi = arp ? (state[ROWS - 1][c] || ARP_SEED[c]) : 0;
        if (arp && !state[ROWS - 1][c]) semi = 0;
        var freq = 110 * Math.pow(2, semi / 12);   // A2 root
        voice(nextTime, freq, dur);
      }
      dly.delayTime.setTargetAtTime(stepDur() * 3, ctx.currentTime, 0.05);
      queue.push({ t: nextTime, col: c });
      nextTime += stepDur();
      ++step;
    }
  }

  function playhead() {
    if (!playing) return;
    var now = ctx.currentTime;
    while (queue.length && queue[0].t <= now) {
      var ev = queue.shift();
      for (var r = 0; r < ROWS; ++r) {
        var el = cellAt(r, ev.col);
        el.style.outline = '2px solid currentColor';
        (function (e) {
          setTimeout(function () { e.style.outline = ''; }, 110);
        })(el);
      }
    }
    requestAnimationFrame(playhead);
  }

  function start() {
    ensureAudio();
    if (ctx.state === 'suspended') ctx.resume();
    playing = true; step = 0; queue.length = 0;
    nextTime = ctx.currentTime + 0.06;
    timer = setInterval(schedule, 25);
    playBtn.textContent = 'STOP';
    requestAnimationFrame(playhead);
  }

  function stop() {
    playing = false;
    if (timer) { clearInterval(timer); timer = null; }
    queue.length = 0;
    playBtn.textContent = 'PLAY';
  }

  playBtn.addEventListener('click', function () {
    if (playing) stop(); else start();
  });
  bpmInput.addEventListener('input', function () {
    bpmVal.textContent = bpmInput.value;
  });
})();

/* =========================================================
   Motion & 3D.
   Same rules as the rest of this page: no dependencies, no
   network requests, no tracking — and nothing that moves when
   the visitor asked for less motion.

     1. tunnel   hand-rolled perspective: rings laid out in depth
                 and projected by 1/z, two wobbles out of step so
                 the rings breathe instead of staying clean
                 circles. The palette is read from the current
                 skin's custom properties and re-read whenever
                 data-theme changes, so it follows the THEME
                 button like everything else. Paused on a hidden
                 tab and capped at ~30 fps, the same rate the
                 plugin's own swirl runs at.
     2. tilt     rotateX/rotateY from the pointer position inside
                 the block, a translateZ pop, and a glare layer
                 that travels with the pointer across the frames.
     3. drift    a few pixels of scroll parallax on those same
                 blocks and on the section headings, so the page
                 reads as layers rather than one sheet.

   The transform is written inline on each block, so the reveal
   animation is untouched: until the pointer lands on a block,
   its transform still belongs to .reveal.
   ========================================================= */
(function () {
  'use strict';

  var root = document.documentElement;
  var reduce = window.matchMedia('(prefers-reduced-motion: reduce)');
  var coarse = window.matchMedia('(hover: none)');

  /* ---------------------------------------------------------
     1. the tunnel — the canvas inside .swirl
     --------------------------------------------------------- */
  function initTunnel() {
    var canvas = document.getElementById('tunnel');
    if (!canvas || !canvas.getContext) return;

    var ctx = canvas.getContext('2d');
    if (!ctx) return;

    // One ring every GAP world units of depth, wrapping at SPAN, so a ring is
    // always somewhere on its way in. Touch devices get the same picture with
    // fewer lines: this is a background, not the subject.
    var RINGS = coarse.matches ? 15 : 22;
    var POINTS = coarse.matches ? 28 : 40;
    var GAP = 0.42, SPAN = RINGS * GAP, RADIUS = 0.46;
    var tint = ['168,85,247', '34,211,238', '236,72,153'];

    var w = 0, h = 0, dpr = 1, depth = 0, twist = 0, last = 0, raf = 0, running = false;

    function rgbOf(value) {
      var hex = String(value).replace('#', '').trim();
      if (hex.length === 3) hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
      var n = parseInt(hex.slice(0, 6), 16);
      if (!isFinite(n)) return '168,85,247';
      return ((n >> 16) & 255) + ',' + ((n >> 8) & 255) + ',' + (n & 255);
    }

    function readTint() {
      ['--accent', '--accent-2', '--accent-3'].forEach(function (name, i) {
        tint[i] = rgbOf(getComputedStyle(root).getPropertyValue(name));
      });
    }

    function resize() {
      dpr = Math.min(window.devicePixelRatio || 1, 2);
      w = canvas.clientWidth;
      h = canvas.clientHeight;
      canvas.width = Math.max(1, Math.round(w * dpr));
      canvas.height = Math.max(1, Math.round(h * dpr));
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    }

    function draw() {
      ctx.clearRect(0, 0, w, h);
      if (!w || !h) return;

      var focal = Math.min(w, h) * 0.92;          // pinhole distance, px
      var cx = w * 0.5 + Math.sin(depth * 0.31) * w * 0.04;
      var cy = h * 0.42 + Math.cos(depth * 0.23) * h * 0.03;

      // Back to front, so near rings paint over far ones.
      for (var i = RINGS - 1; i >= 0; --i) {
        var d = 0.08 + ((i * GAP + depth) % SPAN);
        var fade = Math.min(1, d / 0.7) * Math.max(0, 1 - d / SPAN);
        if (fade < 0.02) continue;

        var scale = focal / d;
        var r = RADIUS * scale;
        var wob = depth * 1.5 + i * 0.37;
        var spin = twist + i * 0.05;

        ctx.beginPath();
        for (var p = 0; p <= POINTS; ++p) {
          var a = p / POINTS * Math.PI * 2 + spin;
          var rr = r * (1 + 0.13 * Math.sin(3 * a + wob) + 0.07 * Math.sin(5 * a - wob * 1.3));
          var x = cx + Math.cos(a) * rr;
          var y = cy + Math.sin(a) * rr;
          if (p === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = 'rgba(' + tint[i % 3] + ',' + (fade * 0.5).toFixed(3) + ')';
        ctx.lineWidth = Math.max(0.5, Math.min(3, 1.15 / d));
        ctx.stroke();
      }
    }

    function frame(now) {
      raf = requestAnimationFrame(frame);
      if (!w || !h) { resize(); return; }

      if (!last) { last = now; return; }           // first frame only measures

      var dt = (now - last) / 1000;
      if (dt < 1 / 30) return;                     // ~30 fps, as in the plugin
      last = now;
      depth += dt * 0.85;
      twist += dt * 0.05;
      draw();
    }

    function start() {
      if (running || reduce.matches) return;
      running = true;
      last = 0;
      raf = requestAnimationFrame(frame);
    }

    function halt() {
      running = false;
      if (raf) cancelAnimationFrame(raf);
      raf = 0;
    }

    function still() {                            // one frame, then nothing moves
      halt();
      resize();
      readTint();
      draw();
    }

    readTint();

    // Paint before the loop starts, so the tunnel is never briefly blank - and
    // so a single still frame is what a browser that never ticks requestFrame
    // still shows.
    if (reduce.matches) { still(); }
    else { resize(); draw(); start(); }

    window.addEventListener('resize', function () {
      resize();
      if (!running) draw();
    }, { passive: true });

    document.addEventListener('visibilitychange', function () {
      if (document.hidden) halt(); else start();
    });

    if (window.MutationObserver) {
      new MutationObserver(function () {
        readTint();
        if (!running) draw();
      }).observe(root, { attributes: true, attributeFilter: ['data-theme'] });
    }

    if (reduce.addEventListener) {
      reduce.addEventListener('change', function () { if (reduce.matches) still(); else start(); });
    }
  }

  /* ---------------------------------------------------------
     2 + 3. tilt and drift
     --------------------------------------------------------- */
  var TILT = '.hero-shot, .shot, .card, .preset, .theme-card, .price-card, .deep';
  var DRIFT = '.section-head';
  var MAX_DEG = 6.5;        // furthest the pointer can lean a block
  var LIFT = 4;             // px the block rises under the pointer
  var POP_SHOT = 22;        // px of translateZ: screenshots lean out further
  var POP_CARD = 9;
  var DRIFT_PX = 13;        // px of parallax at the top/bottom of the viewport

  function initMotion() {
    var moving = [];
    var queued = false;

    function paint(m) {
      if (reduce.matches) { m.el.style.transform = ''; return; }

      m.el.style.transform =
        'perspective(1100px)' +
        ' rotateX(' + m.rx.toFixed(2) + 'deg)' +
        ' rotateY(' + m.ry.toFixed(2) + 'deg)' +
        ' translate3d(0,' + (m.py + (m.over ? -LIFT : 0)).toFixed(2) + 'px,' +
          (m.over ? m.dz : 0) + 'px)';

      if (m.glare) {
        m.el.style.setProperty('--mx', m.mx);
        m.el.style.setProperty('--my', m.my);
      }
    }

    function add(el, tilts) {
      var m = { el: el, tilts: tilts, rx: 0, ry: 0, dz: 0, py: 0, mx: '50%', my: '50%',
                over: false, inView: false, glare: false };
      el._motion = m;
      moving.push(m);
      if (tilts) el.classList.add('tilt');
      if (io) io.observe(el);
      return m;
    }

    function wire(m) {
      var el = m.el;

      el.addEventListener('pointerenter', function () {
        if (reduce.matches || coarse.matches) return;
        m.over = true;
        m.dz = /shot/.test(el.className) ? POP_SHOT : POP_CARD;
        el.classList.add('is-over');
        el.style.transition = 'transform .25s cubic-bezier(.22,.68,.24,1)';
        paint(m);
      });

      el.addEventListener('pointermove', function (e) {
        if (reduce.matches || coarse.matches || !m.over) return;
        var r = el.getBoundingClientRect();
        if (!r.width || !r.height) return;
        var px = (e.clientX - r.left) / r.width;
        var py = (e.clientY - r.top) / r.height;
        m.ry = (px - 0.5) * 2 * MAX_DEG;
        m.rx = (0.5 - py) * 2 * MAX_DEG;
        m.mx = Math.round(px * 100) + '%';
        m.my = Math.round(py * 100) + '%';
        paint(m);
      });

      el.addEventListener('pointerleave', function () {
        if (!m.over) return;
        m.over = false;
        m.dz = 0;
        m.rx = 0;
        m.ry = 0;
        m.mx = '50%';
        m.my = '50%';
        el.classList.remove('is-over');
        el.style.transition = 'transform .55s cubic-bezier(.22,.68,.24,1)';
        paint(m);
      });
    }

    function schedule() {
      if (queued) return;
      queued = true;
      requestAnimationFrame(function () {
        queued = false;
        if (reduce.matches) return;

        var vh = window.innerHeight || 1;

        for (var i = 0; i < moving.length; ++i) {
          var m = moving[i];
          if (!m.inView || m.over) continue;

          var r = m.el.getBoundingClientRect();
          var off = (r.top + r.height / 2 - vh / 2) / vh;          // -1 … 1
          var want = Math.max(-1, Math.min(1, off)) * (m.tilts ? -DRIFT_PX * 0.6 : -DRIFT_PX);
          if (Math.abs(want - m.py) < 0.3) continue;

          m.py = want;
          paint(m);
        }
      });
    }

    var io = null;
    if (window.IntersectionObserver) {
      io = new IntersectionObserver(function (entries) {
        entries.forEach(function (entry) {
          var m = entry.target._motion;
          if (m) m.inView = entry.isIntersecting;
        });
        schedule();
      }, { rootMargin: '12% 0px 12% 0px' });
    }

    var targets = [].slice.call(document.querySelectorAll(TILT));
    targets.forEach(function (el) { add(el, true); });

    // Screenshots get a glare that follows the pointer; a paragraph of copy
    // does not, so the layer only goes inside the figures.
    moving.forEach(function (m) {
      if (!/shot/.test(m.el.className)) return;
      var host = m.el.querySelector('.shot-frame') || m.el;
      var glare = document.createElement('span');
      glare.className = 'glare';
      glare.setAttribute('aria-hidden', 'true');
      host.appendChild(glare);
      m.glare = true;
    });

    moving.forEach(wire);
    [].forEach.call(document.querySelectorAll(DRIFT), function (el) { add(el, false); });

    window.addEventListener('scroll', schedule, { passive: true });
    window.addEventListener('resize', schedule, { passive: true });

    if (reduce.addEventListener) {
      reduce.addEventListener('change', function () {
        if (!reduce.matches) { schedule(); return; }
        moving.forEach(function (m) {
          m.el.style.transition = '';
          m.el.style.transform = '';
          m.el.classList.remove('is-over');
        });
      });
    }

    schedule();
  }

  /* ---------------------------------------------------------
     4. the chain pulse walks along the row instead of all at once
     --------------------------------------------------------- */
  function initSweeps() {
    [].forEach.call(document.querySelectorAll('.chain li'), function (li, i) {
      li.style.animationDelay = (i * 0.17).toFixed(2) + 's';
    });
  }

  initTunnel();
  initMotion();
  initSweeps();
})();
