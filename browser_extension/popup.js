const input = document.getElementById("offset");
const status = document.getElementById("status");

chrome.storage.local.get({ siteOffsetSeconds: 0 }).then((settings) => {
  if (!Number.isFinite(settings.siteOffsetSeconds)) throw new Error("Invalid saved offset");
  input.value = settings.siteOffsetSeconds;
}).catch((error) => { status.textContent = "Could not read offset: " + error; });

document.getElementById("offset-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const value = input.valueAsNumber;
  if (!Number.isFinite(value)) {
    status.textContent = "Enter a finite number of seconds.";
    return;
  }
  chrome.storage.local.set({ siteOffsetSeconds: value }).then(() => {
    status.textContent = "Saved.";
  }).catch((error) => { status.textContent = "Could not save offset: " + error; });
});
