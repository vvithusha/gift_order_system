// Shared helpers: API calls, header/navigation, toast, HTML escaping.
const SHOP_NAME = "Personalized Gift Shop";   // change the shop name here
const SHOP_TAGLINE = "Personalized Gifts, Special Moments";

const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const rs = n => "Rs. " + Number(n).toLocaleString("en-US");

async function api(url, opts = {}) {
  const res = await fetch(url, opts);
  let data = {};
  try { data = await res.json(); } catch (e) {}
  if (!res.ok) { const err = new Error(data.error || "Request failed"); err.status = res.status; throw err; }
  return data;
}
const post = (url, body, method = "POST") => api(url, {method, headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)});

function toast(msg) {
  let t = document.getElementById("toast");
  if (!t) { t = document.createElement("div"); t.id = "toast"; document.body.appendChild(t); }
  t.textContent = msg; t.style.display = "block";
  clearTimeout(t._h); t._h = setTimeout(() => t.style.display = "none", 2600);
}

async function renderHeader(active) {
  const me = await api("/api/me");
  const link = (href, label, key) => `<a href="${href}" class="${active === key ? "on" : ""}">${label}</a>`;
  let nav = link("/", "Home", "home") + link("/#products", "Products", "products");
  if (me.loggedIn && me.role === "admin") nav += link("/admin.html", "Admin", "admin");
  else nav += link("/myorders.html", "My Orders", "orders");
  nav += me.loggedIn ? `<a href="#" id="logoutLink">Logout (${esc(me.name.split(" ")[0])})</a>` : link("/login.html", "Login", "login");
  document.querySelector("header.top").innerHTML =
    `<a class="brand" href="/"><span class="logo">🎁</span><span><b>${esc(SHOP_NAME)}</b><small>${esc(SHOP_TAGLINE)}</small></span></a><nav>${nav}</nav>`;
  const lo = document.getElementById("logoutLink");
  if (lo) lo.onclick = async e => { e.preventDefault(); await post("/api/logout", {}); location.href = "/"; };
  return me;
}
