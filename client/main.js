// Entry point — replaces the React component with vanilla DOM manipulation.
// Handles the canvas, username prompt, chat bar, and connect dialog.

import { Game } from "./game.js";
import { State } from "./engine.js";

let game = null;
let connectStatus = "";
let showConnect = false;

function init() {
  const canvas = document.getElementById("game-canvas");

  try {
    game = new Game(canvas);
    game.start();
  } catch (e) {
    console.error("OrbitFight init error:", e);
    showError(e?.stack || e?.message || String(e));
  }

  // Wire up chat bar
  const chatInput = document.getElementById("chat-input");
  const chatSend = document.getElementById("chat-send");

  function sendChat() {
    const msg = chatInput.value.trim();
    if (!msg) return;
    chatInput.value = "";
    if (game?.netClient) {
      game.netClient.sendChat(msg);
    } else {
      // Single-player: just display locally
      State.name = State.name || "Player";
      console.log(`[${State.name}]: ${msg}`);
    }
  }

  chatSend.addEventListener("click", sendChat);
  chatInput.addEventListener("keydown", (e) => {
    e.stopPropagation(); // prevent game from receiving key events
    if (e.key === "Enter") {
      sendChat();
    }
  });

  // Prevent key events from reaching the game when typing in chat
  chatInput.addEventListener("keyup", (e) => e.stopPropagation());
  chatInput.addEventListener("keypress", (e) => e.stopPropagation());

  // Wire up connect dialog
  document.getElementById("connect-submit").addEventListener("click", (e) => {
    e.preventDefault();
    handleConnect();
  });
  document.getElementById("connect-cancel").addEventListener("click", () => {
    hideConnectDialog();
  });
  // Allow Enter in the proxy-url or player-name field to submit
  document.getElementById("proxy-url").addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); handleConnect(); }
  });
  document.getElementById("player-name").addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); handleConnect(); }
  });

  // Wire up username dialog
  document.getElementById("username-confirm").addEventListener("click", () => {
    const name = document.getElementById("username-input").value.trim() || "Player";
    localStorage.setItem("orbitfight_username", name);
    State.name = name;
    document.getElementById("username-dialog").style.display = "none";
    game.proceedWithAutoConnect(name);
  });
  document.getElementById("username-cancel").addEventListener("click", () => {
    document.getElementById("username-dialog").style.display = "none";
    game.cancelAutoConnect();
  });
  document.getElementById("username-input").addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      document.getElementById("username-confirm").click();
    }
  });

  // Poll for wantConnect flag from the game
  setInterval(() => {
    if (game?.wantConnect) {
      game.wantConnect = false;
      showConnect = true;
      renderConnectDialog();
    }
    // Update connection indicator
    const isConnected = !!game?.netClient;
    const indicator = document.getElementById("conn-indicator");
    if (indicator) {
      indicator.style.display = isConnected ? "block" : "none";
    }
  }, 200);
}

function showError(msg) {
  // Could add an error overlay, for now just console
  console.error(msg);
}

function renderConnectDialog() {
  const dialog = document.getElementById("connect-dialog");
  dialog.style.display = "flex";

  // Prefill player name from localStorage
  const savedName = localStorage.getItem("orbitfight_username");
  if (savedName) {
    document.getElementById("player-name").value = savedName;
  }

  const statusEl = document.getElementById("connect-status");
  statusEl.textContent = connectStatus;
  statusEl.style.color = connectStatus.startsWith("Failed") ? "#f88" :
    connectStatus === "Connected!" ? "#8f8" :
    connectStatus ? "#ff8" : "transparent";
}

function hideConnectDialog() {
  document.getElementById("connect-dialog").style.display = "none";
  connectStatus = "";
}

async function handleConnect() {
  const proxyUrl = document.getElementById("proxy-url").value.trim();
  const name = document.getElementById("player-name").value.trim() || "Player";

  if (!proxyUrl) return;

  // Save username
  localStorage.setItem("orbitfight_username", name);
  State.name = name;

  connectStatus = "Connecting...";
  renderConnectDialog();

  try {
    await game.connectToServer(proxyUrl, name);
    connectStatus = "Connected!";
    renderConnectDialog();
    setTimeout(() => {
      hideConnectDialog();
    }, 800);
  } catch (e) {
    connectStatus = `Failed: ${e?.message || e}`;
    renderConnectDialog();
  }
}

// Start when DOM is ready
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
