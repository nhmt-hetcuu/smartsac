'use strict';

/* ─── Network banner ──────────────────────────────────────── */
function updateNetworkBanner() {
    var b = document.getElementById('offline-banner');
    if (!b) return;
    b.classList.toggle('hidden', navigator.onLine);
}

/* ─── Service Worker ──────────────────────────────────────── */
if ('serviceWorker' in navigator) {
    window.addEventListener('load', function () {
        navigator.serviceWorker.register((window.APP_BASE || '') + '/sw.js').catch(function () {});
    });
}

window.addEventListener('online', updateNetworkBanner);
window.addEventListener('offline', updateNetworkBanner);

/* ─── Page loader ─────────────────────────────────────────── */
function hidePageLoader() {
    var l = document.getElementById('page-loader');
    if (!l) return;
    l.style.opacity = '0';
    l.style.transition = 'opacity 0.35s';
    setTimeout(function () { l.style.display = 'none'; }, 360);
}

function resetLoadingState() {
    var ld = document.getElementById('loading');
    if (ld) { ld.classList.add('hidden'); }
    hidePageLoader();
    document.body.style.overflow = '';
}

window.addEventListener('load', function () { resetLoadingState(); updateNetworkBanner(); });
window.addEventListener('pageshow', resetLoadingState);

// Safety fallback: force-hide loader after 5 s if 'load' never fires
setTimeout(function () {
    var l = document.getElementById('page-loader');
    if (l && l.style.display !== 'none') { hidePageLoader(); }
}, 5000);

/* ─── Carousel ────────────────────────────────────────────── */
(function () {
    var track = document.getElementById('carousel-track');
    var dotsWrap = document.getElementById('carousel-dots');
    if (!track) return;

    var slides = track.children;
    var total = slides.length;
    var idx = 0;
    var timer;

    // Build dot / tab buttons
    for (var i = 0; i < total; i++) {
        (function (n) {
            var d = document.createElement('button');
            d.className = 'carousel-dot' + (n === 0 ? ' active' : '');
            d.setAttribute('role', 'tab');
            d.setAttribute('aria-label', 'Chuyển đến slide ' + (n + 1) + ' / ' + total);
            d.setAttribute('aria-selected', n === 0 ? 'true' : 'false');
            d.onclick = function () { goTo(n); };
            dotsWrap.appendChild(d);
        })(i);
    }

    function updateDots() {
        Array.from(dotsWrap.children).forEach(function (d, i) {
            var active = i === idx;
            d.classList.toggle('active', active);
            d.setAttribute('aria-selected', active ? 'true' : 'false');
        });
        // Announce current slide to screen readers via aria-live on the track
        track.setAttribute('aria-label', 'Slide ' + (idx + 1) + ' / ' + total);
    }

    function goTo(n) {
        idx = (n + total) % total;
        track.style.transform = 'translateX(-' + (idx * 100) + '%)';
        updateDots();
    }

    // Exposed globally for inline onclick handlers in the HTML
    window.carouselMove = function (dir) { goTo(idx + dir); resetAuto(); };

    function startAuto() { timer = setInterval(function () { goTo(idx + 1); }, 4500); }
    function stopAuto()  { clearInterval(timer); }
    function resetAuto() { stopAuto(); startAuto(); }

    // Pause auto-play when the carousel is scrolled out of view (saves CPU / battery)
    if ('IntersectionObserver' in window) {
        var carouselObs = new IntersectionObserver(function (entries) {
            entries.forEach(function (entry) {
                if (entry.isIntersecting) { resetAuto(); } else { stopAuto(); }
            });
        }, { threshold: 0.2 });
        carouselObs.observe(track.parentElement);
    } else {
        resetAuto();
    }

    // Pause on hover
    track.parentElement.addEventListener('mouseenter', stopAuto);
    track.parentElement.addEventListener('mouseleave', resetAuto);

    // Swipe support
    var sx = 0;
    track.addEventListener('touchstart', function (e) { sx = e.touches[0].clientX; }, { passive: true });
    track.addEventListener('touchend', function (e) {
        var dx = e.changedTouches[0].clientX - sx;
        if (Math.abs(dx) > 40) { window.carouselMove(dx < 0 ? 1 : -1); }
    });
})();

/* ─── Lazy-load iframes (YouTube / embeds) ────────────────── */
/*
 * Use data-src on iframes instead of src so they don't block the
 * initial page load. The observer swaps data-src → src when the
 * element is ~300 px away from the viewport.
 */
(function () {
    var lazyFrames = document.querySelectorAll('iframe[data-src]');
    if (!lazyFrames.length) return;

    if ('IntersectionObserver' in window) {
        var frameObs = new IntersectionObserver(function (entries, obs) {
            entries.forEach(function (entry) {
                if (!entry.isIntersecting) return;
                var el = entry.target;
                el.src = el.dataset.src;
                el.removeAttribute('data-src');
                obs.unobserve(el);
            });
        }, { rootMargin: '300px 0px' });

        lazyFrames.forEach(function (f) { frameObs.observe(f); });
    } else {
        // Fallback for very old browsers
        lazyFrames.forEach(function (f) { f.src = f.dataset.src; });
    }
})();

/* ─── Lazy-load images with data-src / data-srcset ───────── */
/*
 * For carousel or any future images: add loading="lazy" AND
 * data-src="real-url" on the <img> tag. The observer does the swap.
 */
(function () {
    var lazyImgs = document.querySelectorAll('img[data-src]');
    if (!lazyImgs.length) return;

    if ('IntersectionObserver' in window) {
        var imgObs = new IntersectionObserver(function (entries, obs) {
            entries.forEach(function (entry) {
                if (!entry.isIntersecting) return;
                var img = entry.target;
                img.src = img.dataset.src;
                if (img.dataset.srcset) { img.srcset = img.dataset.srcset; }
                img.removeAttribute('data-src');
                obs.unobserve(img);
            });
        }, { rootMargin: '300px 0px' });

        lazyImgs.forEach(function (img) { imgObs.observe(img); });
    } else {
        lazyImgs.forEach(function (img) {
            img.src = img.dataset.src;
            if (img.dataset.srcset) { img.srcset = img.dataset.srcset; }
        });
    }
})();

/* ─── Section scroll-reveal ───────────────────────────────── */
/*
 * Elements with class="reveal" fade in when they enter the viewport.
 * CSS only applies the animation when prefers-reduced-motion: no-preference,
 * so users who prefer reduced motion see the content immediately.
 */
(function () {
    var reveals = document.querySelectorAll('.reveal');
    if (!reveals.length) return;

    if ('IntersectionObserver' in window) {
        var revealObs = new IntersectionObserver(function (entries) {
            entries.forEach(function (entry) {
                if (entry.isIntersecting) {
                    entry.target.classList.add('visible');
                }
            });
        }, { rootMargin: '0px 0px -60px 0px', threshold: 0.08 });

        reveals.forEach(function (el) { revealObs.observe(el); });
    } else {
        // No IntersectionObserver → show everything immediately
        reveals.forEach(function (el) { el.classList.add('visible'); });
    }
})();

/* ─── Domain redirect modal ───────────────────────────────── */
(function () {
    var OLD = 'smartsac.xyz';
    if (location.hostname === OLD || location.hostname === 'www.' + OLD) {
        var modal = document.getElementById('domain-modal');
        if (modal) { modal.classList.remove('hidden'); }
    }
})();

function goNewDomain() {
    window.location.href = 'https://hetcuu.com/smartsac' + location.pathname + location.search;
}
function closeDomainModal() {
    var modal = document.getElementById('domain-modal');
    if (modal) { modal.classList.add('hidden'); }
}

/* ─── Auth / Dashboard transition ────────────────────────── */
function showAuth() {
    var ld = document.getElementById('loading');
    if (ld) { ld.classList.remove('hidden'); }
    setTimeout(function () { window.location.href = (window.APP_BASE || '') + '/check'; }, 80);
}

/* ─── Lightbox core ───────────────────────────────────────── */
function openLightboxSrc(src, alt, caption) {
    var lb    = document.getElementById('lightbox');
    var lbImg = document.getElementById('lightbox-img');
    var lbCap = document.getElementById('lightbox-caption');
    if (!lb || !lbImg) return;
    lbImg.src = src;
    lbImg.alt = alt || '';
    if (lbCap) { lbCap.textContent = caption || ''; }
    lb.classList.remove('hidden');
    document.body.style.overflow = 'hidden';
}

/* Called by onclick on .cert-thumb figures */
function openLightbox(el) {
    var img = el.querySelector('.cert-thumb-img');
    if (!img || !img.getAttribute('src')) return;
    var cap = el.querySelector('figcaption');
    openLightboxSrc(img.src, img.alt, cap ? cap.textContent.trim() : '');
}

function closeLightbox() {
    var lb = document.getElementById('lightbox');
    if (!lb || lb.classList.contains('hidden')) return;
    lb.classList.add('hidden');
    var lbImg = document.getElementById('lightbox-img');
    if (lbImg) { lbImg.src = ''; }
    document.body.style.overflow = '';
}

document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') { closeLightbox(); }
});

/* ─── Carousel image lightbox ─────────────────────────────── */
(function () {
    document.querySelectorAll('.carousel-slide img').forEach(function (img) {
        img.addEventListener('click', function () {
            if (!img.src) return;
            var slide = img.closest('.carousel-slide');
            var capEl = slide ? slide.querySelector('.slide-caption') : null;
            openLightboxSrc(img.src, img.alt, capEl ? capEl.textContent.trim() : '');
        });
    });
})();
