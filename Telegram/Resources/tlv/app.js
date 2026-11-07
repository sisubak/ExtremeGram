(function () {
  "use strict";

  const VECTOR_CONSTRUCTOR = 0x1cb5c415;
  const BOOL_FALSE = 0xbc799737;
  const BOOL_TRUE = 0x997275b5;
  const BOOL_TRUE_ALT = 0x3fedd339;
  const NULL_CONSTRUCTOR = 0x56730bcc;
  const TWO_POW_32 = 0x100000000;

  const schema = window.TL_SCHEMA_FULL;

  const elements = {
    status: document.getElementById("status"),
    tree: document.getElementById("tree"),
    copyButton: document.getElementById("copyButton"),
  };
  let currentJsonText = "";
  let statusResetTimer = 0;

  function applyDesktopTheme() {
    const theme = window.TelegramDesktopTLVTheme;
    if (!theme || typeof theme !== "object") {
      return;
    }

    const colors = {
      "--td-bg": theme.bg_color,
      "--td-secondary-bg": theme.secondary_bg_color,
      "--td-text": theme.text_color,
      "--td-hint": theme.hint_color,
      "--td-link": theme.link_color,
      "--td-button": theme.button_color,
      "--td-button-text": theme.button_text_color,
      "--td-section": theme.section_bg_color,
      "--td-section-header": theme.section_header_text_color,
      "--td-destructive": theme.destructive_text_color,
    };

    for (const [name, value] of Object.entries(colors)) {
      if (typeof value === "string") {
        document.documentElement.style.setProperty(name, value);
      }
    }
  }

  window.addEventListener("tdesktop-theme-changed", applyDesktopTheme);
  applyDesktopTheme();

  class UnknownObjectIdError extends Error {
    constructor(objectId) {
      super(`Unknown object id: 0x${objectId.toString(16)}`);
      this.name = "UnknownObjectIdError";
      this.objectId = objectId;
    }
  }

  class TLReader {
    constructor(objectsMap, buffer, offset) {
      this.objectsMap = objectsMap;
      this.position = offset || 0;

      if (ArrayBuffer.isView(buffer)) {
        this.view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
        this.uint8 = new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength);
      } else {
        this.view = new DataView(buffer);
        this.uint8 = new Uint8Array(buffer);
      }
    }

    static deserializeObject(objectsMap, bytes, offset) {
      return new TLReader(objectsMap, bytes, offset || 0).object();
    }

    int() {
      const value = this.view.getInt32(this.position, true);
      this.position += 4;
      return value;
    }

    uint() {
      const value = this.view.getUint32(this.position, true);
      this.position += 4;
      return value;
    }

    int53() {
      const low = this.view.getInt32(this.position, true) >>> 0;
      const high = this.view.getInt32(this.position + 4, true);
      this.position += 8;
      return low + TWO_POW_32 * high;
    }

    long(unsigned) {
      const low = BigInt(this.view.getUint32(this.position, true));
      const highGetter = unsigned ? "getUint32" : "getInt32";
      const high = BigInt(this.view[highGetter](this.position + 4, true));
      this.position += 8;
      return (high << 32n) | low;
    }

    double() {
      const value = this.view.getFloat64(this.position, true);
      this.position += 8;
      return value;
    }

    bytes() {
      const first = this.uint8[this.position++];
      let length;
      let padding;

      if (first === 254) {
        length =
          this.uint8[this.position++] |
          (this.uint8[this.position++] << 8) |
          (this.uint8[this.position++] << 16);
        padding = length % 4;
      } else {
        length = first;
        padding = (length + 1) % 4;
      }

      const value = this.raw(length);
      if (padding > 0) {
        this.position += 4 - padding;
      }
      return value;
    }

    string() {
      return new TextDecoder().decode(this.bytes());
    }

    raw(length) {
      const size = length === undefined ? this.uint8.length - this.position : length;
      const value = this.uint8.subarray(this.position, this.position + size);
      this.position += size;
      return value;
    }

    vector(itemReader, bare) {
      if (!bare) {
        const constructor = this.uint();
        if (constructor !== VECTOR_CONSTRUCTOR) {
          throw new Error(
            `Invalid vector constructor: expected 0x${VECTOR_CONSTRUCTOR.toString(16)}, got 0x${constructor.toString(16)}`
          );
        }
      }

      const count = this.uint();
      const values = [];

      for (let index = 0; index < count; index += 1) {
        values.push(itemReader.call(this));
      }

      return values;
    }

    object(constructorId) {
      const objectId = constructorId === undefined ? this.uint() : constructorId;

      if (objectId === VECTOR_CONSTRUCTOR) {
        return this.vector(this.object, true);
      }
      if (objectId === BOOL_FALSE) {
        return false;
      }
      if (objectId === BOOL_TRUE || objectId === BOOL_TRUE_ALT) {
        return true;
      }
      if (objectId === NULL_CONSTRUCTOR) {
        return null;
      }

      const parser = this.objectsMap[objectId];
      if (!parser) {
        this.position -= 4;
        throw new UnknownObjectIdError(objectId);
      }

      return parser(this);
    }
  }

  function decodeBase64Url(input) {
    const normalized = input.replace(/-/g, "+").replace(/_/g, "/");
    const padded = normalized + "=".repeat((4 - (normalized.length % 4 || 4)) % 4);
    const decoded = atob(padded);
    const bytes = new Uint8Array(decoded.length);

    for (let index = 0; index < decoded.length; index += 1) {
      bytes[index] = decoded.charCodeAt(index);
    }

    return bytes;
  }

  function encodeBase64(bytes) {
    let text = "";
    for (let index = 0; index < bytes.length; index += 1) {
      text += String.fromCharCode(bytes[index]);
    }
    return btoa(text);
  }

  function toSnakeCase(value) {
    return value.replace(/([a-z])([A-Z])/g, "$1_$2").toLowerCase();
  }

  function normalizeValue(value) {
    if (value === null || value === undefined) {
      return value;
    }
    if (typeof value === "bigint") {
      return value.toString();
    }
    if (value instanceof Uint8Array) {
      return encodeBase64(value);
    }
    if (Array.isArray(value)) {
      return value.map(normalizeValue);
    }
    if (typeof value === "object") {
      if (typeof value.toJSON === "function") {
        return normalizeValue(value.toJSON());
      }

      const output = {};
      for (const key of Object.keys(value)) {
        const item = value[key];
        if (item !== null && item !== undefined) {
          output[toSnakeCase(key)] = normalizeValue(item);
        }
      }
      return output;
    }
    return value;
  }

  function unwrapMessageContainer(value) {
    if (
      value &&
      typeof value === "object" &&
      (value._ === "messages.messages" || value._ === "messages.channelMessages") &&
      Array.isArray(value.messages) &&
      value.messages.length > 0
    ) {
      return value.messages[0];
    }
    return value;
  }

  function parsePayload(payload) {
    const bytes = decodeBase64Url(payload);
    return TLReader.deserializeObject(schema, bytes);
  }

  function setStatus(message, type, options) {
    if (statusResetTimer) {
      window.clearTimeout(statusResetTimer);
      statusResetTimer = 0;
    }

    elements.status.textContent = message;
    elements.status.className = "status" + (type ? ` ${type}` : "");

    if (options && options.toast) {
      elements.status.classList.add("toast");
      statusResetTimer = window.setTimeout(function () {
        setStatus("", "");
      }, options.duration || 1800);
    }

    elements.status.hidden = !message;
  }

  function escapeHtml(text) {
    return String(text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/\"/g, "&quot;");
  }

  function valueClassName(value) {
    if (value === null) {
      return "null";
    }
    if (typeof value === "string") {
      return "string";
    }
    if (typeof value === "number") {
      return "number";
    }
    if (typeof value === "boolean") {
      return "boolean";
    }
    return "";
  }

  function formatPrimitive(value, key, layer) {
    if (typeof value === "string") {
      const jsonString = JSON.stringify(value);
      const escapedJsonString = escapeHtml(jsonString);

      if (key === "_" && layer) {
        const href = `https://schema.jppgr.am/layer/${encodeURIComponent(layer)}/${encodeURIComponent(value)}`;
        const escapedContent = escapeHtml(jsonString.slice(1, -1));
        return `"${`<a class="constructor-link" href="${href}" target="_blank" rel="noreferrer">${escapedContent}</a>`}"`;
      }
      return escapedJsonString;
    }
    if (value === null) {
      return "null";
    }
    return escapeHtml(String(value));
  }

  function buildLine(key, contentHtml, suffix) {
    const line = document.createElement("div");
    line.className = "tree-line";

    if (key !== null) {
      const keyNode = document.createElement("span");
      keyNode.className = "key";
      keyNode.textContent = `"${key}"`;
      line.appendChild(keyNode);
      line.appendChild(document.createTextNode(": "));
    }

    const valueNode = document.createElement("span");
    valueNode.className = "value";
    valueNode.innerHTML = contentHtml;
    line.appendChild(valueNode);

    if (suffix) {
      line.appendChild(document.createTextNode(suffix));
    }
    return line;
  }

  function appendContainerBlock(container, value, layer, closingSuffix) {
    const isArray = Array.isArray(value);
    const entries = isArray
      ? value.map((item, index) => [String(index), item])
      : Object.entries(value);

    const children = document.createElement("div");
    children.className = "tree-children";

    entries.forEach(function (entry, index) {
      children.appendChild(renderTreeValue(entry[1], entry[0], layer, index < entries.length - 1 ? "," : ""));
    });

    const closingLine = document.createElement("div");
    closingLine.className = "tree-line tree-line-closing";
    closingLine.textContent = `${isArray ? "]" : "}"}${closingSuffix}`;

    container.appendChild(children);
    container.appendChild(closingLine);
  }

  function renderTreeValue(value, key, layer, suffix) {
    if (value === null || typeof value !== "object") {
      const line = buildLine(key, formatPrimitive(value, key, layer), suffix);
      line.querySelector(".value").classList.add(valueClassName(value));
      return line;
    }

    const isArray = Array.isArray(value);
    const wrapper = document.createElement("div");
    wrapper.className = key === null ? "tree-block root" : "tree-block";

    if (key === null) {
      const openingLine = document.createElement("div");
      openingLine.className = "tree-line";
      openingLine.textContent = isArray ? "[" : "{";
      wrapper.appendChild(openingLine);
      appendContainerBlock(wrapper, value, layer, suffix);
      return wrapper;
    }

    const openingLine = buildLine(key, isArray ? "[" : "{", "");
    wrapper.appendChild(openingLine);
    appendContainerBlock(wrapper, value, layer, suffix);
    return wrapper;
  }

  function clearOutput() {
    elements.tree.innerHTML = "";
    currentJsonText = "";
  }

  function writeToNativeClipboard(text) {
    if (!window.TelegramDesktopTLVClipboardBridge
        || !window.external
        || typeof window.external.invoke !== "function") {
      return false;
    }

    window.external.invoke(JSON.stringify({
      type: "clipboard_write",
      text,
    }));
    return true;
  }

  async function copyJson() {
    if (!currentJsonText) {
      setStatus("No JSON to copy.", "error");
      return;
    }

    if (writeToNativeClipboard(currentJsonText)) {
      setStatus("Copied to clipboard.", "success", { toast: true, duration: 1800 });
      return;
    }

    try {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(currentJsonText);
      } else {
        const helper = document.createElement("textarea");
        helper.value = currentJsonText;
        helper.setAttribute("readonly", "true");
        helper.style.position = "absolute";
        helper.style.left = "-9999px";
        document.body.appendChild(helper);
        helper.select();
        document.execCommand("copy");
        helper.remove();
      }

      setStatus("Copied to clipboard.", "success", { toast: true, duration: 1800 });
    } catch (error) {
      setStatus(`Failed to copy: ${error instanceof Error ? error.message : String(error)}`, "error");
    }
  }

  function parseFromHash() {
    clearOutput();

    const params = new URLSearchParams(window.location.hash.replace(/^#/, ""));
    const payload = (params.get("m") || "").trim();
    const layer = (params.get("l") || "").trim();
    const unwrap = params.get("unwrap") !== "0";

    if (!payload) {
      setStatus("", "");
      return;
    }

    try {
      setStatus("Loading...", "");
      const parsed = parsePayload(payload);
      const target = unwrap ? unwrapMessageContainer(parsed) : parsed;
      const normalized = normalizeValue(target);
      currentJsonText = JSON.stringify(normalized, null, 2);

      elements.tree.appendChild(renderTreeValue(normalized, null, layer, ""));
      setStatus("", "");
    } catch (error) {
      const message =
        error instanceof UnknownObjectIdError
          ? `${error.message}. Constructor not found in current schema.`
          : error instanceof Error
            ? error.message
            : String(error);

      setStatus(message, "error");
    }
  }

  function loadFromHash() {
    parseFromHash();
  }

  elements.copyButton.addEventListener("click", copyJson);
  document.addEventListener("keydown", (event) => {
    if (!event.ctrlKey
        || event.altKey
        || event.metaKey
        || event.key.toLowerCase() !== "c") {
      return;
    }
    const selection = window.getSelection();
    const text = selection ? selection.toString() : "";
    if (text && writeToNativeClipboard(text)) {
      event.preventDefault();
    }
  });
  window.addEventListener("hashchange", loadFromHash);

  if (!schema || typeof schema !== "object") {
    setStatus("schema.js not loaded properly.", "error");
    return;
  }

  loadFromHash();
})();
