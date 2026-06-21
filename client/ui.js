// Ported from include/ui.hpp and src/ui.cpp, simplified for web mode.
// Now includes a ChatPanel for message display.

import { State, g_camera, pushMessage, getMessages } from "./engine.js";
import { TAU, dst } from "./math.js";

export class MiscInfoUI {
  constructor() {
    this.active = true;
    this.padding = 5;
    this.width = 0;
    this.height = 0;
  }

  update(ctx) {
    let info = `FPS: ${State.framerate}\n`;
    info += `Entities: ${State.updateGroup.length}\n`;
    info += `Kills: ${State.kills}\n`;
    if (State.lastPing > 0) {
      info += `Ping: ${Math.round(State.lastPing * 1000)}ms\n`;
    }
    if (State.lastTrajectoryRef) {
      const distVal = Math.floor(dst(State.ownX - State.lastTrajectoryRef.x, State.ownY - State.lastTrajectoryRef.y));
      info += `Distance: ${distVal}\n`;
      if (State.ownEntity) {
        const v = Math.floor(dst(State.ownEntity.velX - State.lastTrajectoryRef.velX, State.ownEntity.velY - State.lastTrajectoryRef.velY));
        info += `Velocity: ${v}\n`;
      }
    }

    const lines = info.split("\n");
    ctx.font = `${State.textCharacterSize}px ui-monospace, SFMono-Regular, "Menlo", monospace`;
    ctx.textBaseline = "top";
    ctx.textAlign = "left";
    let maxW = 0;
    for (const ln of lines) maxW = Math.max(maxW, ctx.measureText(ln).width);
    this.width = maxW + this.padding * 2;
    this.height = lines.length * (State.textCharacterSize + 3) + this.padding * 2;

    ctx.fillStyle = "rgba(40,40,40,0.55)";
    ctx.strokeStyle = "rgba(20,20,20,0.75)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.rect(this.padding, this.padding, this.width, this.height);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = "rgba(255,255,255,0.95)";
    for (let i = 0; i < lines.length; i++) {
      ctx.fillText(lines[i], this.padding * 2, this.padding * 2 + i * (State.textCharacterSize + 3));
    }
  }
}

// Chat panel — displays the last N messages above the chat input bar.
export class ChatPanel {
  constructor() {
    this.active = true;
    this.maxMessages = 12;
    this.fontSize = 13;
    this.lineHeight = 16;
  }

  update(ctx) {
    const messages = getMessages();
    if (messages.length === 0) return;

    const showCount = Math.min(this.maxMessages, messages.length);
    const start = Math.max(0, messages.length - showCount);

    ctx.font = `${this.fontSize}px ui-monospace, SFMono-Regular, "Menlo", monospace`;
    ctx.textBaseline = "bottom";
    ctx.textAlign = "left";

    const chatBarHeight = 32; // approx height of the chat input bar
    const startY = g_camera.h - chatBarHeight - 4;

    for (let i = 0; i < showCount; i++) {
      const msg = messages[start + i];
      const y = startY - (showCount - 1 - i) * this.lineHeight - 2;

      // Truncate long messages
      let displayMsg = msg;
      const maxWidth = g_camera.w * 0.6;
      if (ctx.measureText(displayMsg).width > maxWidth) {
        while (displayMsg.length > 0 && ctx.measureText(displayMsg + "…").width > maxWidth) {
          displayMsg = displayMsg.slice(0, -1);
        }
        displayMsg += "…";
      }

      // Background for readability
      const textWidth = ctx.measureText(displayMsg).width;
      ctx.fillStyle = "rgba(0,0,0,0.5)";
      ctx.fillRect(6, y - this.fontSize, textWidth + 8, this.lineHeight);

      // Text
      ctx.fillStyle = "rgba(255,255,255,0.9)";
      ctx.fillText(displayMsg, 10, y);
    }

    ctx.textBaseline = "top";
  }
}

export class HelpUI {
  constructor() {
    this.active = false;
    this.padding = 8;
    this.visibleUntil = 0;
  }

  show(seconds) {
    this.visibleUntil = State.globalTime + seconds;
    this.active = true;
  }

  update(ctx) {
    if (!this.active) return;
    if (State.globalTime > this.visibleUntil) {
      this.active = false;
      return;
    }
    const lines = [
      "CONTROLS",
      "W / S       — forward / back",
      "A / D       — rotate left / right",
      "X           — fire railgun",
      "Space       — fire missile",
      "E           — boost",
      "Q           — toggle slow rotate (precision)",
      "T           — target body near cursor",
      "Tab         — set reference body for trajectory",
      "Enter       — focus chat",
      "Mouse wheel — zoom",
      "Esc         — toggle menu",
      "H           — toggle this help",
      "",
      "A new system generates automatically.",
      "Press Tab near a body to see predicted orbits.",
      "Trajectories only appear after Tab is pressed.",
    ];
    ctx.font = `${State.textCharacterSize}px ui-monospace, SFMono-Regular, "Menlo", monospace`;
    ctx.textBaseline = "top";
    ctx.textAlign = "left";
    let maxW = 0;
    for (const ln of lines) maxW = Math.max(maxW, ctx.measureText(ln).width);
    const w = maxW + this.padding * 2;
    const h = lines.length * (State.textCharacterSize + 3) + this.padding * 2;
    const x = (g_camera.w - w) / 2;
    const y = (g_camera.h - h) / 2;
    ctx.fillStyle = "rgba(20,20,28,0.92)";
    ctx.strokeStyle = "rgba(140,140,160,0.9)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.rect(x, y, w, h);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = "rgba(255,255,255,0.95)";
    for (let i = 0; i < lines.length; i++) {
      ctx.fillText(lines[i], x + this.padding, y + this.padding + i * (State.textCharacterSize + 3));
    }
  }
}

export class MenuUI {
  constructor() {
    this.active = true;
    this.buttons = [];
    this.title = "ORBITFIGHT";
    this.subtitle = "(web port of Ilya246/orbitfight)";
  }

  resized() {}

  layout() {
    const w = Math.min(360, g_camera.w * 0.7);
    const maxButtons = State.ownEntity ? (State.authority ? 5 : 4) : 3;
    const h = 80 + maxButtons * 46 + 20;
    const x = (g_camera.w - w) / 2;
    const y = (g_camera.h - h) / 2;
    return { x, y, w, h };
  }

  getButtons() {
    const { x, y, w } = this.layout();
    const btns = [];
    let labels;
    if (State.ownEntity) {
      if (State.authority) {
        labels = ["Resume", "Reset System", "Connect to Server", "How to Play", "Center View"];
      } else {
        labels = ["Resume", "Disconnect", "How to Play", "Center View"];
      }
    } else {
      labels = ["Freeplay", "Connect to Server", "How to Play"];
    }
    const btnH = 36;
    const gap = 10;
    const startY = y + 80;
    for (let i = 0; i < labels.length; i++) {
      btns.push({
        label: labels[i],
        x: x + 20,
        y: startY + i * (btnH + gap),
        w: w - 40,
        h: btnH,
      });
    }
    this.buttons = btns;
    return btns;
  }

  hitTest(mx, my) {
    for (const b of this.getButtons()) {
      if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) return b;
    }
    return null;
  }

  update(ctx) {
    const { x, y, w, h } = this.layout();
    ctx.fillStyle = "rgba(15,15,25,0.92)";
    ctx.strokeStyle = "rgba(140,140,200,0.85)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.rect(x, y, w, h);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = "rgba(255,229,97,1)";
    ctx.font = `bold 32px ui-monospace, SFMono-Regular, "Menlo", monospace`;
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    ctx.fillText(this.title, x + w / 2, y + 14);
    ctx.fillStyle = "rgba(180,180,200,0.85)";
    ctx.font = `12px ui-monospace, SFMono-Regular, "Menlo", monospace`;
    ctx.fillText(this.subtitle, x + w / 2, y + 52);

    const buttons = this.getButtons();
    for (const b of buttons) {
      ctx.fillStyle = "rgba(60,60,80,0.9)";
      ctx.strokeStyle = "rgba(180,180,220,0.7)";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.rect(b.x, b.y, b.w, b.h);
      ctx.fill();
      ctx.stroke();
      ctx.fillStyle = "rgba(255,255,255,0.95)";
      ctx.font = `16px ui-monospace, SFMono-Regular, "Menlo", monospace`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(b.label, b.x + b.w / 2, b.y + b.h / 2);
    }
    ctx.textAlign = "left";
    ctx.textBaseline = "top";
  }
}
