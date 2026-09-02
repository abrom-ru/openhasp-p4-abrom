"use strict";

// Shared logic for /config/<section> forms.
//
// Fetches /api/config, populates the fields listed in `fields`, and on
// submit sends a PATCH-style POST { <section>: { ...current values } }.
// Passwords in the loaded payload arrive as "******" (masked by
// hasp_config.cpp); we treat that as "leave unchanged" — either we send
// it back verbatim (server-side set_config ignores "******") or drop it
// if the user typed something.
//
// `types` maps field → "int"|"float"|"bool" for coerced JSON values.
// Anything unset defaults to string.
function bindConfigForm(section, fields, types) {
  types = types || {};
  const form = document.getElementById("f");
  const status = document.getElementById("status");

  function setStatus(msg, cls) {
    status.textContent = msg;
    status.className = cls || "";
  }

  function coerce(name, value) {
    const t = types[name];
    if (t === "int")   return parseInt(value, 10);
    if (t === "float") return parseFloat(value);
    if (t === "bool")  return value === "true" || value === "1" || value === true;
    return value;
  }

  async function load() {
    try {
      const r = await fetch("/api/config", { credentials: "same-origin" });
      if (!r.ok) throw new Error("HTTP " + r.status);
      const cfg = await r.json();
      const s = cfg[section] || {};

      for (const name of fields) {
        const el = document.getElementById(name);
        if (!el) continue;

        const v = s[name];
        if (v === undefined || v === null) continue;

        // Password fields: show placeholder-only, don't pre-fill masked "******"
        // into a visible field (would tempt user to save unchanged).
        if (el.type === "password") {
          el.value = "";
        } else {
          el.value = v;
        }
      }
    } catch (e) {
      setStatus("Не удалось загрузить: " + e.message, "err");
    }
  }

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    setStatus("Сохраняю…", "info");

    const payload = {};
    for (const name of fields) {
      const el = document.getElementById(name);
      if (!el) continue;

      if (el.type === "password") {
        if (el.value === "") continue; // leave server value untouched
        payload[name] = el.value;
      } else {
        payload[name] = coerce(name, el.value);
      }
    }

    try {
      const r = await fetch("/api/config", {
        method: "POST",
        credentials: "same-origin",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ [section]: payload }),
      });
      if (!r.ok) throw new Error("HTTP " + r.status);
      await r.json();
      setStatus("Сохранено.", "ok");
      // Clear password field so a subsequent submit doesn't resend it.
      for (const name of fields) {
        const el = document.getElementById(name);
        if (el && el.type === "password") el.value = "";
      }
    } catch (e) {
      setStatus("Ошибка: " + e.message, "err");
    }
  });

  load();
}
