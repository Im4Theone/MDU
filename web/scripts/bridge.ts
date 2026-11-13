// Typed wrapper around the WebView2 message channel.

export type DockEdge = "bottom" | "left" | "right";
export type Motion = "system" | "reduced" | "full";

export interface DockItem {
  id: string;
  name: string;
  icon: string;
}

export interface StateMessage {
  type: "state";
  dock: { edge: DockEdge; iconSize: number; padding: number; gap: number; shadowInset: number };
  motion: Motion;
  theme: { variables: Record<string, string>; stylesheet: string };
  items: DockItem[];
}

export type OutgoingMessage = { type: "ready" } | { type: "launch"; id: string };

interface WebViewHost {
  postMessage(message: string): void;
  addEventListener(type: "message", listener: (event: { data: unknown }) => void): void;
}

declare global {
  interface Window {
    chrome?: { webview?: WebViewHost };
  }
}

function host(): WebViewHost | undefined {
  return window.chrome?.webview;
}

export function send(message: OutgoingMessage): void {
  // The native side expects a JSON string and validates it independently.
  host()?.postMessage(JSON.stringify(message));
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function isStateMessage(value: unknown): value is StateMessage {
  if (!isObject(value) || value.type !== "state") return false;
  const { dock, theme, items } = value;
  return isObject(dock) && typeof dock.iconSize === "number" && isObject(theme) && Array.isArray(items);
}

export function onState(listener: (state: StateMessage) => void): void {
  host()?.addEventListener("message", (event) => {
    if (isStateMessage(event.data)) {
      listener(event.data);
    }
  });
}
