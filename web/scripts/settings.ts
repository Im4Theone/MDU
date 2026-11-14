import { onMessage, post } from "./bridge.js";

// --- Protocol types (mirrors src/ui/settings_protocol.cpp) -------------------

interface DockSettings {
  enabled: boolean;
  edge: "bottom" | "left" | "right";
  monitors: "all" | "primary";
  placement: "reserve" | "overlay";
  iconSize: number;
  margin: number;
}

interface Settings {
  startWithWindows: boolean;
  theme: string;
  motion: "system" | "reduced" | "full";
  dock: DockSettings;
  wallpaper: { enabled: boolean };
}

interface Item {
  id: string;
  name: string;
  target: string;
  icon: string;
}

interface State {
  type: "settings-state";
  version: string;
  settings: Settings;
  items: Item[];
  themes: { id: string; name: string }[];
  wallpaperImage: string;
  notice: string;
  theme: { variables: Record<string, string>; stylesheet: string };
}

const THEME_HOST = "https://themes.mdu.example/";

let state: State | null = null;
const reducedMotionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");

function element<T extends HTMLElement>(id: string): T {
  return document.getElementById(id) as T;
}

function isState(value: unknown): value is State {
  if (typeof value !== "object" || value === null) return false;
  const candidate = value as Partial<State>;
  return candidate.type === "settings-state" && typeof candidate.settings === "object" && Array.isArray(candidate.items);
}

// --- Rendering -------------------------------------------------------------

function applyTheme(theme: State["theme"]): void {
  const root = document.documentElement.style;
  for (const [name, value] of Object.entries(theme.variables)) {
    root.setProperty(`--${name}`, value);
  }
  const href = theme.stylesheet.startsWith(THEME_HOST) ? theme.stylesheet : "";
  let link = document.getElementById("theme-stylesheet") as HTMLLinkElement | null;
  if (!href) {
    link?.remove();
    return;
  }
  if (!link) {
    link = document.createElement("link");
    link.id = "theme-stylesheet";
    link.rel = "stylesheet";
    document.head.appendChild(link);
  }
  if (link.href !== href) link.href = href;
}

function applyMotion(): void {
  const preference = state?.settings.motion ?? "system";
  const reduce = preference === "reduced" || (preference === "system" && reducedMotionQuery.matches);
  document.documentElement.classList.toggle("reduce-motion", reduce);
}

function settingValue(settings: Settings, path: string): unknown {
  return path.split(".").reduce<unknown>((value, key) => (value as Record<string, unknown>)[key], settings);
}

function renderControls(settings: Settings): void {
  for (const input of document.querySelectorAll<HTMLInputElement | HTMLSelectElement>("[data-setting]")) {
    const value = settingValue(settings, input.dataset.setting ?? "");
    if (input instanceof HTMLInputElement && input.type === "checkbox") {
      input.checked = value === true;
    } else if (input instanceof HTMLInputElement && input.type === "radio") {
      input.checked = input.value === value;
    } else if (document.activeElement !== input) {
      input.value = String(value);
    }
  }
  element<HTMLOutputElement>("icon-size-value").textContent = `${settings.dock.iconSize} px`;
  element<HTMLOutputElement>("margin-value").textContent = `${settings.dock.margin} px`;
}

function renderThemes(current: State): void {
  const select = element<HTMLSelectElement>("theme");
  select.replaceChildren(
    ...current.themes.map((theme) => {
      const option = document.createElement("option");
      option.value = theme.id;
      option.textContent = theme.name;
      return option;
    }),
  );
  select.value = current.settings.theme;
}

function initials(name: string): string {
  const letters = name.split(/\s+/).filter(Boolean).map((word) => word[0]).join("");
  return (letters || "?").slice(0, 2).toUpperCase();
}

function iconButton(label: string, glyph: string, action: string, disabled: boolean): HTMLButtonElement {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "icon-button";
  button.textContent = glyph;
  button.title = label;
  button.setAttribute("aria-label", label);
  button.dataset.action = action;
  button.disabled = disabled;
  return button;
}

function renderItems(items: Item[]): void {
  const list = element<HTMLUListElement>("items");
  const focusedId = (document.activeElement as HTMLElement | null)?.closest<HTMLElement>(".item")?.dataset.id;
  const focusedAction = (document.activeElement as HTMLElement | null)?.dataset.action;

  list.replaceChildren(
    ...items.map((item, index) => {
      const row = document.createElement("li");
      row.className = "item";
      row.dataset.id = item.id;

      if (item.icon.startsWith("data:image/")) {
        const image = document.createElement("img");
        image.src = item.icon;
        image.alt = "";
        row.appendChild(image);
      } else {
        const fallback = document.createElement("span");
        fallback.className = "fallback";
        fallback.textContent = initials(item.name);
        row.appendChild(fallback);
      }

      const details = document.createElement("div");
      details.className = "details";
      const name = document.createElement("input");
      name.type = "text";
      name.value = item.name;
      name.maxLength = 64;
      name.setAttribute("aria-label", `Name of ${item.name}`);
      name.dataset.action = "rename";
      const target = document.createElement("p");
      target.className = "target";
      target.textContent = item.target;
      target.title = item.target;
      details.append(name, target);
      row.appendChild(details);

      row.append(
        iconButton(`Move ${item.name} up`, "↑", "up", index === 0),
        iconButton(`Move ${item.name} down`, "↓", "down", index === items.length - 1),
        iconButton(`Remove ${item.name}`, "✕", "remove", false),
      );
      return row;
    }),
  );
  element<HTMLParagraphElement>("items-empty").hidden = items.length > 0;

  // Keep keyboard focus where it was after re-rendering.
  if (focusedId && focusedAction) {
    list.querySelector<HTMLElement>(`.item[data-id="${CSS.escape(focusedId)}"] [data-action="${focusedAction}"]`)?.focus();
  }
}

function render(next: State): void {
  state = next;
  applyTheme(next.theme);
  applyMotion();
  element<HTMLSpanElement>("version").textContent = next.version;
  renderThemes(next);
  renderControls(next.settings);
  renderItems(next.items);
  element<HTMLParagraphElement>("wallpaper-image").textContent = next.wallpaperImage
    ? next.wallpaperImage
    : "The theme's own image";
  element<HTMLButtonElement>("reset-wallpaper").disabled = next.wallpaperImage === "";

  const notice = element<HTMLParagraphElement>("notice");
  notice.textContent = next.notice;
  notice.hidden = next.notice === "";
  document.querySelector("main")?.setAttribute("aria-busy", "false");
}

// --- Events ---------------------------------------------------------------

function patchFor(path: string, value: unknown): object {
  const keys = path.split(".");
  return keys.reduceRight<unknown>((inner, key) => ({ [key]: inner }), value) as object;
}

document.addEventListener("change", (event) => {
  const input = event.target as HTMLInputElement | HTMLSelectElement;
  const path = input.dataset.setting;
  if (path) {
    let value: unknown = input.value;
    if (input instanceof HTMLInputElement && input.type === "checkbox") value = input.checked;
    if (input instanceof HTMLInputElement && input.type === "range") value = Number(input.value);
    post({ type: "settings-update", settings: patchFor(path, value) });
    return;
  }
  if (input.dataset.action === "rename") {
    commitRename(input as HTMLInputElement);
  }
});

// Live value labels while dragging; the change itself is sent on release.
document.addEventListener("input", (event) => {
  const input = event.target as HTMLInputElement;
  if (input.type === "range") {
    const output = document.querySelector<HTMLOutputElement>(`output[for="${input.id}"]`);
    if (output) output.textContent = `${input.value} px`;
  }
});

function commitRename(input: HTMLInputElement): void {
  const id = input.closest<HTMLElement>(".item")?.dataset.id;
  const item = state?.items.find((candidate) => candidate.id === id);
  const name = input.value.trim();
  if (!item || !id) return;
  if (name === "" || name === item.name) {
    input.value = item.name;
    return;
  }
  post({ type: "settings-rename", id, name });
}

document.addEventListener("keydown", (event) => {
  const input = event.target as HTMLElement;
  if (input.dataset.action === "rename" && event.key === "Enter") {
    (input as HTMLInputElement).blur();
  } else if (input.dataset.action === "rename" && event.key === "Escape") {
    const id = input.closest<HTMLElement>(".item")?.dataset.id;
    (input as HTMLInputElement).value = state?.items.find((item) => item.id === id)?.name ?? "";
    (input as HTMLInputElement).blur();
  }
});

element<HTMLUListElement>("items").addEventListener("click", (event) => {
  const button = (event.target as Element).closest<HTMLButtonElement>("button[data-action]");
  const id = button?.closest<HTMLElement>(".item")?.dataset.id;
  if (!button || !id || !state) return;

  const order = state.items.map((item) => item.id);
  const index = order.indexOf(id);
  if (button.dataset.action === "remove") {
    order.splice(index, 1);
  } else {
    const target = button.dataset.action === "up" ? index - 1 : index + 1;
    if (target < 0 || target >= order.length) return;
    [order[index], order[target]] = [order[target], order[index]];
  }
  post({ type: "settings-items", order });
});

element<HTMLButtonElement>("add-item").addEventListener("click", () => post({ type: "settings-add-item" }));
element<HTMLButtonElement>("choose-wallpaper").addEventListener("click", () =>
  post({ type: "settings-choose-wallpaper" }),
);
element<HTMLButtonElement>("reset-wallpaper").addEventListener("click", () =>
  post({ type: "settings-reset-wallpaper" }),
);
element<HTMLButtonElement>("open-file").addEventListener("click", () => post({ type: "settings-open-file" }));

document.addEventListener("contextmenu", (event) => {
  if (!(event.target instanceof HTMLInputElement && event.target.type === "text")) event.preventDefault();
});

reducedMotionQuery.addEventListener("change", applyMotion);
onMessage((data) => {
  if (isState(data)) render(data);
});
post({ type: "settings-ready" });
