import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
CHROME = Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Google" / "Chrome" / "Application" / "chrome.exe"


@pytest.mark.skipif(os.name != "nt" or not CHROME.exists(), reason="Chrome required")
def test_popup_persists_offset_and_background_applies_it(tmp_path):
    background = (ROOT / "browser_extension" / "background.js").read_text(encoding="utf-8")
    popup = (ROOT / "browser_extension" / "popup.js").read_text(encoding="utf-8")
    html = """
<form id="offset-form"><input id="offset" type="number"><button>Save</button></form>
<p id="status"></p><pre id="result">NOT RUN</pre>
<script>
let saved = 7.5, listener, httpOk = true;
window.chrome = {
  storage: {local: {
    get: async () => ({siteOffsetSeconds: saved}),
    set: async value => { saved = value.siteOffsetSeconds; }
  }},
  runtime: {onMessage: {addListener: fn => { listener = fn; }}}
};
window.fetch = async () => ({
  ok: httpOk, status: 503,
  json: async () => ({get: 100, accel: 1, running: true, paused: false, seq: 1, offset: 0})
});
</script><script>
""" + background + """
</script><script>
""" + popup + """
</script><script>
setTimeout(async () => {
  try {
    const input = document.getElementById("offset");
    if (input.value !== "7.5") throw new Error("saved value was not loaded");
    input.value = "-12.5";
    document.getElementById("offset-form").dispatchEvent(new Event("submit", {cancelable: true}));
    await new Promise(resolve => setTimeout(resolve, 0));
    if (saved !== -12.5 || document.getElementById("status").textContent !== "Saved.")
      throw new Error("offset was not persisted");
    const read = () => new Promise(resolve => listener({type: "state"}, null, resolve));
    const response = await read();
    if (!response.ok || response.state.get !== 100 || response.state.offset !== -12.5)
      throw new Error("background did not apply saved site offset");
    httpOk = false;
    if ((await read()).ok) throw new Error("HTTP error was not surfaced");
    saved = NaN;
    httpOk = true;
    if ((await read()).ok) throw new Error("invalid saved offset was not surfaced");
    document.getElementById("result").textContent = "PASS";
  } catch (error) {
    document.getElementById("result").textContent = "FAIL: " + error;
  }
}, 0);
</script>
"""
    page = tmp_path / "extension.html"
    page.write_text(html, encoding="utf-8")
    result = subprocess.run(
        [str(CHROME), "--headless", "--disable-gpu", "--no-first-run",
         f"--user-data-dir={tmp_path / 'profile'}", "--virtual-time-budget=2000",
         "--dump-dom", page.as_uri()],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert '<pre id="result">PASS</pre>' in result.stdout, result.stdout
