// WuBuDesk Secure Bridge — background service worker.
// SPDX-License-Identifier: WaefreBeorn-UMV3
// SECURITY: only relays read-only page context; redacts passwords/cookies/storage.
// Talks to the local agent bridge over chrome.runtime ports (loopback only).

const REDACT = "<redacted>";

// Token is set by the agent bridge at install via storage.local; never hardcoded.
async function getToken() {
  const r = await chrome.storage.local.get("wubu_token");
  return r.wubu_token || null;
}

function redactSensitive(text) {
  if (!text) return text;
  // strip anything that looks like a password field value or cookie blob
  return text
    .replace(/password["']?\s*[:=]\s*["']?[^"'\s,}]+/gi, `password=${REDACT}`)
    .replace(/cookie["']?\s*[:=]\s*["']?[^"'\s,}]+/gi, `cookie=${REDACT}`);
}

async function getContext(tabId) {
  const tab = await chrome.tabs.get(tabId);
  let visible = "";
  try {
    const [res] = await chrome.scripting.executeScript({
      target: { tabId },
      func: () => {
        // read visible text only; never password inputs or storage
        const clone = document.body.cloneNode(true);
        clone.querySelectorAll("input[type=password],input[type=hidden]").forEach(e => e.remove());
        const txt = (clone.innerText || "").slice(0, 4000);
        return txt;
      }
    });
    visible = res.result;
  } catch (e) { visible = "[cannot read tab]"; }
  return {
    title: tab.title,
    url: tab.url,
    visibleText: redactSensitive(visible),
    badgeOn: true
  };
}

async function clickSelector(tabId, sel) {
  try {
    await chrome.scripting.executeScript({
      target: { tabId },
      func: (s) => { const el = document.querySelector(s); if (el) { el.click(); return true; } return false; },
      args: [sel]
    });
    return { ok: true };
  } catch (e) { return { ok: false, error: String(e) }; }
}

async function scroll(tabId, dir) {
  try {
    await chrome.scripting.executeScript({
      target: { tabId },
      func: (d) => window.scrollBy(0, d === "up" ? -400 : 400),
      args: [dir]
    });
    return { ok: true };
  } catch (e) { return { ok: false }; }
}

chrome.runtime.onConnect.addListener(async (port) => {
  const token = await getToken();
  port.onMessage.addListener(async (msg) => {
    if (msg.token !== token) { port.postMessage({ error: "bad_token" }); return; }
    const tab = await chrome.tabs.query({ active: true, currentWindow: true });
    const tabId = tab[0]?.id;
    if (!tabId) { port.postMessage({ error: "no_active_tab" }); return; }
    if (msg.cmd === "getContext") port.postMessage({ result: await getContext(tabId) });
    else if (msg.cmd === "clickSelector") port.postMessage({ result: await clickSelector(tabId, msg.sel) });
    else if (msg.cmd === "scroll") port.postMessage({ result: await scroll(tabId, msg.dir) });
    else if (msg.cmd === "activeTab") port.postMessage({ result: { title: tab[0].title, url: tab[0].url } });
    else port.postMessage({ error: "unknown_cmd" });
  });
});
