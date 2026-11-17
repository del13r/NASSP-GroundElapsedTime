// Fetch the plugin state on behalf of the page, adding the browser-only offset.
chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (msg && msg.type === "state") {
    Promise.all([
      fetch("http://127.0.0.1:8765/", { cache: "no-store" }).then((r) => {
        if (!r.ok) throw new Error("HTTP " + r.status);
        return r.json();
      }),
      chrome.storage.local.get({ siteOffsetSeconds: 0 })
    ])
      .then(([s, settings]) => {
        if (!Number.isFinite(settings.siteOffsetSeconds)) throw new Error("Invalid site offset");
        sendResponse({ ok: true, state: { ...s, offset: settings.siteOffsetSeconds } });
      })
      .catch((e) => sendResponse({ ok: false, error: String(e) }));
    return true;
  }
});
