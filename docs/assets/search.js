// Search for the wiki (#578): a box on the wiki's front page that finds words
// in any wiki page and links to the heading they are under.
//
// THE PAGES ARE THE INDEX. On the first search the script reads every page in
// the wiki's side list, as published, and splits each at its headings; the
// browser keeps them (about 75 KB in all, compressed, in 2026-10). A page
// added to docs/_data/wiki.yml is searched with nothing else to do, and a
// heading's link is the id the page itself carries.
//
// NOTHING LEAVES THE READER'S DEVICE: the pages come from this site, the
// search runs here, and nothing is counted or sent.
//
// THE RESULTS LIE OVER THE PAGE, in a panel under the box, so nothing on the
// page moves (MMagTech, 2026-10-25). Tapping outside it, Escape, or emptying
// the box closes it; it scrolls by itself when the results are long.
//
// The box is made here, so a browser without scripts shows no box that
// cannot work. The layout loads this on the wiki's front page only
// (docs/_layouts/wiki.html); the top bar's Search link goes to #search.
(function () {
  "use strict";

  var main = document.querySelector("main.doc");
  var title = main && main.querySelector("h1");
  if (!title) return;

  // ---- The box ----------------------------------------------------------

  var form = document.createElement("form");
  form.className = "search";
  form.id = "search";
  form.setAttribute("role", "search");
  form.innerHTML =
    '<label for="search-box">Search the wiki</label>' +
    '<input id="search-box" type="search" placeholder="Search the wiki" ' +
    'autocomplete="off" autocapitalize="off" spellcheck="false" enterkeyhint="search" ' +
    'aria-controls="search-panel" aria-expanded="false">' +
    '<div class="search-panel" id="search-panel" hidden>' +
    '<p class="search-status" aria-live="polite"></p>' +
    '<ol class="search-results"></ol></div>';
  var after = title.nextElementSibling;
  main.insertBefore(form, after && after.tagName === "P" ? after.nextSibling : title.nextSibling);

  var box = form.querySelector("input");
  var panel = form.querySelector(".search-panel");
  var status = form.querySelector(".search-status");
  var list = form.querySelector(".search-results");

  function open() {
    if (!panel.hidden) return;
    panel.hidden = false;
    box.setAttribute("aria-expanded", "true");
    // On a phone the box goes to the top first, so the panel has the screen
    // below it, keyboard or not.
    if (window.matchMedia("(max-width: 640px)").matches &&
        form.getBoundingClientRect().top > 90)
      form.scrollIntoView({ block: "start" });
  }
  function close() {
    panel.hidden = true;
    box.setAttribute("aria-expanded", "false");
  }

  // ---- Reading the pages --------------------------------------------------

  // Lower case, accents off, ONE CHARACTER FOR EACH CHARACTER, so a place
  // found in the folded text is the same place in the text as written.
  function fold(s) {
    var out = "";
    for (var i = 0; i < s.length; i++) {
      var c = s.charAt(i);
      var f = c.normalize ? c.normalize("NFD").replace(/[̀-ͯ]/g, "") : c;
      f = f.toLowerCase();
      out += f.length ? f.charAt(0) : c;
    }
    return out;
  }

  function squash(s) {
    return s.replace(/\s+/g, " ").trim();
  }

  // One page into its sections: what comes before the first heading is
  // the page's own, then one section for each h2 and h3.
  function sections(doc, url, order) {
    var page = doc.querySelector("main.doc");
    var h1 = page && page.querySelector("h1");
    if (!h1) return [];
    var name = squash(h1.textContent);
    var out = [];
    var at = { page: name, heading: "", id: "", parts: [] };
    for (var el = page.firstElementChild; el; el = el.nextElementSibling) {
      if (el === h1 || el.matches("nav.pager, p.edit, form.search")) continue;
      if (el.tagName === "H2" || el.tagName === "H3") {
        out.push(at);
        at = { page: name, heading: squash(el.textContent), id: el.id, parts: [] };
      } else {
        at.parts.push(el.textContent);
      }
    }
    out.push(at);
    return out.map(function (s) {
      var text = squash(s.parts.join(" "));
      return {
        page: s.page,
        heading: s.heading,
        href: s.id ? url + "#" + s.id : url,
        text: text,
        foldedHead: fold(s.heading),
        foldedPage: fold(s.page),
        foldedText: fold(text),
        order: order,
        weight: /changelog\.html$/.test(url) ? 0.5 : /\/wiki\/$/.test(url) ? 0.6 : 1
      };
    });
  }

  var loading = null;
  function load() {
    if (loading) return loading;
    var seen = {};
    var urls = [];
    document.querySelectorAll(".side .list a[href]").forEach(function (a) {
      var u = new URL(a.getAttribute("href"), location.href);
      u.hash = "";
      u.search = "";
      if (!seen[u.href]) {
        seen[u.href] = true;
        urls.push(u.href);
      }
    });
    loading = Promise.all(
      urls.map(function (u, i) {
        return fetch(u)
          .then(function (r) {
            if (!r.ok) throw new Error(u + ": " + r.status);
            return r.text();
          })
          .then(function (html) {
            var doc = new DOMParser().parseFromString(html, "text/html");
            return sections(doc, new URL(u).pathname, i);
          })
          // A page that did not come is left out of this search, and the
          // next search asks for every page again.
          .catch(function () {
            return null;
          });
      })
    ).then(function (all) {
      var read = all.filter(function (p) { return p !== null; });
      if (read.length < all.length) loading = null;
      if (!read.length) throw new Error("no page of the wiki could be read");
      return [].concat.apply([], read);
    });
    loading.catch(function () {
      loading = null;
    });
    return loading;
  }

  // ---- Finding ---------------------------------------------------------------

  function words(q) {
    return fold(q)
      .split(/[^0-9a-zÀ-￿]+/)
      .filter(function (w) { return w.length > 0; });
  }

  function count(hay, w) {
    var n = 0;
    for (var at = hay.indexOf(w); at >= 0 && n < 5; at = hay.indexOf(w, at + w.length)) n++;
    return n;
  }

  function whole(hay, w) {
    return new RegExp("(^|[^0-9a-z])" + w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + "($|[^0-9a-z])").test(hay);
  }

  // How well a section answers: its score, and which of the words it has.
  function rate(s, ws, phrase) {
    var total = 0;
    var got = [];
    var allInPage = true;
    for (var i = 0; i < ws.length; i++) {
      var w = ws[i];
      var inHead = s.foldedHead.indexOf(w) >= 0;
      var inPage = s.foldedPage.indexOf(w) >= 0;
      var inText = count(s.foldedText, w);
      if (!inPage) allInPage = false;
      if (!inHead && !inPage && !inText) continue;
      got.push(i);
      if (inHead) total += whole(s.foldedHead, w) ? 15 : 10;
      if (inPage) total += 6;
      total += inText + (inText && whole(s.foldedText, w) ? 2 : 0);
    }
    if (ws.length > 1 && (s.foldedHead + " " + s.foldedText).indexOf(phrase) >= 0) total += 8;
    // A search for a page by its name finds the page's opening first.
    if (allInPage && !s.heading) total += 25;
    return { n: total * s.weight, got: got };
  }

  // Every section with all the words, best first. A page whose sections
  // have them between them, with none that has them all ("sftp password"
  // on File access), comes after, as its section with the most of them.
  function find(all, ws) {
    var phrase = ws.join(" ");
    var found = [];
    var complete = {};
    var pages = {};
    all.forEach(function (s) {
      var r = rate(s, ws, phrase);
      if (!r.got.length) return;
      if (r.got.length === ws.length) {
        found.push({ s: s, n: r.n, ws: ws });
        complete[s.page] = true;
        return;
      }
      var p = pages[s.page] || (pages[s.page] = { has: {}, best: null, k: 0, n: 0 });
      r.got.forEach(function (i) { p.has[i] = true; });
      if (r.got.length > p.k || (r.got.length === p.k && r.n > p.n)) {
        p.best = s;
        p.k = r.got.length;
        p.n = r.n;
      }
    });
    Object.keys(pages).forEach(function (name) {
      var p = pages[name];
      if (!complete[name] && Object.keys(p.has).length === ws.length)
        found.push({ s: p.best, n: p.n * 0.5, ws: ws });
    });
    found.sort(function (a, b) { return b.n - a.n || a.s.order - b.s.order; });
    return found;
  }

  // ---- Showing ------------------------------------------------------------------

  // The words found, marked, in a piece of the section around the first.
  function snippet(s, ws) {
    var text = s.text;
    var folded = s.foldedText;
    var first = -1;
    ws.forEach(function (w) {
      var at = folded.indexOf(w);
      if (at >= 0 && (first < 0 || at < first)) first = at;
    });
    var from = 0;
    var to = Math.min(text.length, 170);
    if (first > 60) {
      from = text.lastIndexOf(" ", first - 50);
      from = from < 0 ? 0 : from + 1;
      to = Math.min(text.length, from + 170);
    }
    if (to < text.length) {
      var cut = text.lastIndexOf(" ", to);
      if (cut > from + 80) to = cut;
    }
    var piece = text.slice(from, to);
    var foldedPiece = folded.slice(from, to);
    var marks = [];
    ws.forEach(function (w) {
      for (var at = foldedPiece.indexOf(w); at >= 0; at = foldedPiece.indexOf(w, at + w.length))
        marks.push([at, at + w.length]);
    });
    marks.sort(function (a, b) { return a[0] - b[0]; });
    var p = document.createElement("p");
    if (from > 0) p.appendChild(document.createTextNode("… "));
    var pos = 0;
    marks.forEach(function (m) {
      if (m[0] < pos) {
        if (m[1] > pos) {
          p.lastChild.textContent += piece.slice(pos, m[1]);
          pos = m[1];
        }
        return;
      }
      p.appendChild(document.createTextNode(piece.slice(pos, m[0])));
      var mark = document.createElement("mark");
      mark.textContent = piece.slice(m[0], m[1]);
      p.appendChild(mark);
      pos = m[1];
    });
    p.appendChild(document.createTextNode(piece.slice(pos) + (to < text.length ? " …" : "")));
    return p;
  }

  function show(found, q) {
    list.textContent = "";
    if (!found.length) {
      status.textContent = "Nothing found for “" + q + "”.";
      return;
    }
    status.textContent = found.length === 1 ? "1 result" : found.length + " results";
    found.slice(0, 30).forEach(function (r) {
      var s = r.s;
      var li = document.createElement("li");
      var a = document.createElement("a");
      a.href = s.href;
      var page = document.createElement("span");
      page.className = "search-page";
      page.textContent = s.page;
      a.appendChild(page);
      if (s.heading) a.appendChild(document.createTextNode(" › " + s.heading));
      li.appendChild(a);
      if (s.text) li.appendChild(snippet(s, r.ws));
      list.appendChild(li);
    });
    if (found.length > 30) {
      var more = document.createElement("li");
      more.className = "search-more";
      more.textContent = "The first 30 are shown. More words narrow it down.";
      list.appendChild(more);
    }
  }

  var asked = 0;
  function search() {
    var q = squash(box.value);
    var ask = ++asked;
    try {
      history.replaceState(null, "", q ? "?q=" + encodeURIComponent(q) + "#search" : location.pathname + "#search");
    } catch (e) { /* a page opened from a file */ }
    var ws = words(q);
    if (!ws.length || q.length < 2) {
      list.textContent = "";
      status.textContent = "";
      close();
      return;
    }
    open();
    if (!loading) status.textContent = "Reading the wiki…";
    load().then(
      function (all) {
        if (ask !== asked) return;
        show(find(all, ws), q);
      },
      function () {
        if (ask !== asked) return;
        list.textContent = "";
        status.textContent = "The wiki's pages could not be read. Check the connection and try again.";
      }
    );
  }

  var wait = 0;
  box.addEventListener("input", function () {
    clearTimeout(wait);
    wait = setTimeout(search, 150);
  });
  box.addEventListener("focus", function () {
    load().catch(function () {});
  }, { once: true });
  // Back in the box with results there, they show again, whether the box
  // had lost the cursor or not.
  function reopen() {
    if (list.childElementCount || status.textContent) open();
  }
  box.addEventListener("focus", reopen);
  box.addEventListener("click", reopen);
  document.addEventListener("pointerdown", function (e) {
    if (!panel.hidden && !form.contains(e.target)) close();
  });
  form.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && !panel.hidden) {
      e.preventDefault();
      close();
    }
  });
  // Enter keeps the results and puts the phone's keyboard away.
  form.addEventListener("submit", function (e) {
    e.preventDefault();
    clearTimeout(wait);
    search();
    box.blur();
  });

  // The top bar's Search link lands here. The box did not exist when the
  // browser looked for #search, so it is brought into view by hand; with a
  // search already in the address (coming back from a result) it is not
  // focused, so a phone does not open its keyboard over the results.
  function arrive(focus) {
    if (location.hash !== "#search") return;
    form.scrollIntoView({ block: "start" });
    if (focus) box.focus({ preventScroll: true });
  }
  window.addEventListener("hashchange", function () { arrive(true); });
  var start = new URLSearchParams(location.search).get("q");
  if (start) {
    box.value = start;
    search();
  }
  arrive(!start);
})();
