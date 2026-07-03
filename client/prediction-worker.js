// Web Worker for trajectory prediction.
//
// Runs in a separate thread to avoid stutter on the main thread. Receives
// a serialized game state, delegates to prediction-core.js, and posts back
// trajectories in world coordinates.
//
// Message protocol:
//   Main → Worker: { type: "predict", state }
//   Worker → Main: { type: "done", trajectories, ghostTrajectories, ... }

import { runPredictionCore } from "./prediction-core.js";

let isRunning = false;

self.onmessage = (ev) => {
    const msg = ev.data;
    if (msg.type === "predict" && !isRunning) {
        isRunning = true;
        try {
            const result = runPredictionCore(msg.state);
            self.postMessage({ type: "done", ...result });
        } catch (e) {
            self.postMessage({ type: "error", error: e.message || String(e) });
        }
        isRunning = false;
    }
};
