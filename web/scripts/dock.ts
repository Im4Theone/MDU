import { type DockItem, type Motion, type StateMessage, onState, send } from "./bridge.js";

const THEME_HOST = "https://themes.mdu.example/";

const dock = document.getElementById("dock") as HTMLElement;
const reducedMotionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
let motionPreference: Motion = "system";

function applyMotion(): void {
  const reduce = motionPreference === "reduced" || (motionPreference === "system" && reducedMotionQuery.matches);
  document.documentElement.classList.toggle("reduce-motion", reduce);
}

function applyTheme(theme: StateMessage["theme"]): void {
  const root = document.documentElement.style;
  for (const [name, value] of Object.entries(theme.variables)) {
    root.setProperty(`--${name}`, value);
  }

  // Only stylesheets from the local theme host are accepted.
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
  if (link.href !== href) {
    link.href = href;
  }
}

function initials(name: string): string {
  const letters = name
    .split(/\s+/)
    .filter(Boolean)
    .map((word) => word[0])
    .join("");
  return (letters || "?").slice(0, 2).toUpperCase();
}

function createItem(item: DockItem): HTMLButtonElement {
  const button = document.createElement("button");
  button.className = "item";
  button.type = "button";
  button.title = item.name;
  button.setAttribute("aria-label", item.name);
  button.dataset.id = item.id;

  if (item.icon.startsWith("data:image/")) {
    const image = document.createElement("img");
    image.src = item.icon;
    image.alt = "";
    image.draggable = false;
    button.appendChild(image);
  } else {
    const fallback = document.createElement("span");
    fallback.className = "fallback";
    fallback.textContent = initials(item.name);
    button.appendChild(fallback);
  }
  return button;
}

function render(state: StateMessage): void {
  const { edge, iconSize, padding, gap, shadowInset } = state.dock;
  document.body.dataset.edge = edge;

  const root = document.documentElement.style;
  root.setProperty("--icon-size", `${iconSize}px`);
  root.setProperty("--padding", `${padding}px`);
  root.setProperty("--gap", `${gap}px`);
  root.setProperty("--shadow-inset", `${shadowInset}px`);

  motionPreference = state.motion;
  applyMotion();
  applyTheme(state.theme);

  dock.replaceChildren(...state.items.map(createItem));
  const overflow = edge === "bottom" ? dock.scrollWidth > dock.clientWidth : dock.scrollHeight > dock.clientHeight;
  document.body.classList.toggle("overflow", overflow);
  document.body.classList.add("ready");
}

dock.addEventListener("click", (event) => {
  const button = (event.target as Element).closest<HTMLButtonElement>(".item");
  const id = button?.dataset.id;
  if (!button || !id) return;

  send({ type: "launch", id });
  button.classList.remove("launching");
  void button.offsetWidth; // restart the animation on repeated clicks
  button.classList.add("launching");
});

dock.addEventListener("animationend", (event) => {
  (event.target as Element).classList.remove("launching");
});

// Disable browser behaviours that make no sense in a dock.
document.addEventListener("contextmenu", (event) => event.preventDefault());
document.addEventListener("dragstart", (event) => event.preventDefault());

reducedMotionQuery.addEventListener("change", applyMotion);
onState(render);
send({ type: "ready" });
