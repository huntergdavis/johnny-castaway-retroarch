/*
 * Minimal Johnny Castaway launcher for RetroArch Web.
 *
 * The Emscripten module setup and BrowserFS mount arrangement are derived
 * from RetroArch's pkg/emscripten/libretro/libretro.js at revision
 * 96a1b1a9cf3f9166affcfd7df4323aa58d5c281a (GPL-3.0).
 * See WEB_PLAYER_NOTICE.md for complete provenance and licenses.
 */

const contentInput = document.querySelector("#content-files");
const startButton = document.querySelector("#start");
const menuButton = document.querySelector("#menu");
const resetButton = document.querySelector("#reset");
const fullscreenButton = document.querySelector("#fullscreen");
const statusElement = document.querySelector("#status");
const audioStateElement = document.querySelector("#audio-state");
const audioUnlockButton = document.querySelector("#audio-unlock-button");
const canvas = document.querySelector("#canvas");

const retroarchRoot = "/home/web_user/retroarch";
const corePath = `${retroarchRoot}/cores/johnny_castaway_libretro.core`;
const contentDirectory = `${retroarchRoot}/userdata/content`;
const mapPath = `${contentDirectory}/RESOURCE.MAP`;
const configPath = `${retroarchRoot}/userdata/retroarch.cfg`;
const coreOptionsPath = `${retroarchRoot}/userdata/retroarch-core-options.cfg`;
const localContentPaths = {
  map: "local-content/RESOURCE.MAP",
  archive: "local-content/RESOURCE.001",
};
const originalSoundIds = Array.from({ length: 25 }, (_, id) => id).filter(
  (id) => id !== 11 && id !== 13,
);

let moduleInstance = null;
let running = false;
const trackedAudioContexts = [];
const trackedAudioContextSet = new WeakSet();

function activeAudioContexts() {
  return trackedAudioContexts.filter((context) => context.state !== "closed");
}

function renderAudioState() {
  const contexts = activeAudioContexts();
  const context = contexts[contexts.length - 1] || null;
  audioStateElement.dataset.contextCount = String(contexts.length);
  audioStateElement.dataset.sampleRate = context
    ? String(context.sampleRate)
    : "";
  if (!context) {
    audioStateElement.textContent = running
      ? "Audio: starting…"
      : "Audio: waiting for game";
    audioStateElement.dataset.state = "waiting";
    audioUnlockButton.disabled = true;
    return;
  }
  if (context.state === "running") {
    audioStateElement.textContent = "Audio: on";
    audioStateElement.dataset.state = "running";
    audioUnlockButton.disabled = true;
    return;
  }
  if (context.state === "suspended" || context.state === "interrupted") {
    audioStateElement.textContent = "Audio: locked by browser";
    audioStateElement.dataset.state = "locked";
    audioUnlockButton.disabled = false;
    return;
  }
  audioStateElement.textContent = `Audio: ${context.state}`;
  audioStateElement.dataset.state = "failed";
  audioUnlockButton.disabled = true;
}

function trackAudioContext(context) {
  if (!context || trackedAudioContextSet.has(context)) return;
  trackedAudioContextSet.add(context);
  trackedAudioContexts.push(context);
  context.addEventListener("statechange", renderAudioState);
  renderAudioState();
}

function installAudioContextTracker() {
  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  const prototype = AudioContextClass?.prototype;
  if (!prototype || prototype.__jcAudioContextTrackerInstalled) return;
  const originalCreateBufferSource = prototype.createBufferSource;
  prototype.createBufferSource = function (...createArguments) {
    trackAudioContext(this);
    return originalCreateBufferSource.apply(this, createArguments);
  };
  Object.defineProperty(prototype, "__jcAudioContextTrackerInstalled", {
    value: true,
  });

  // RWebAudio tries resume() before it creates its first buffer source. When
  // autoplay rejects that attempt there is no prototype call from which to
  // discover the already-created context, so retain the real constructor
  // result as a fallback. Subclassing preserves native instanceof/prototype
  // behavior and static inheritance.
  class TrackedAudioContext extends AudioContextClass {
    constructor(...constructorArguments) {
      super(...constructorArguments);
      trackAudioContext(this);
    }
  }
  if (window.AudioContext === AudioContextClass) {
    window.AudioContext = TrackedAudioContext;
  }
  if (window.webkitAudioContext === AudioContextClass) {
    window.webkitAudioContext = TrackedAudioContext;
  }
}

async function unlockAudio() {
  const lockedContexts = activeAudioContexts().filter(
    (context) => context.state !== "running",
  );
  await Promise.allSettled(
    lockedContexts.map(async (context) => {
      try {
        await context.resume();
      } catch (error) {
        console.warn("Could not resume browser audio:", error);
      }
    }),
  );
  renderAudioState();
}

function unlockAudioFromGesture() {
  if (
    activeAudioContexts().some(
      (context) =>
        context.state === "suspended" || context.state === "interrupted",
    )
  ) {
    void unlockAudio();
  }
}

installAudioContextTracker();
renderAudioState();

function installAudioSmokeProbe() {
  if (!new URLSearchParams(window.location.search).has("smoke")) return;

  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (!AudioContextClass) {
    window.__jcAudioProbe = {
      installed: false,
      error: "Web AudioContext is unavailable",
    };
    return;
  }

  const prototype = AudioContextClass.prototype;
  if (prototype.__jcAudioSmokeProbeInstalled) return;

  const probe = {
    installed: true,
    version: 1,
    installedAtMs: performance.now(),
    queuedBuffers: 0,
    endedBuffers: 0,
    framesQueued: 0,
    scheduledSeconds: 0,
    windowGeneration: 0,
    contexts: [],
  };
  const contextRecords = new WeakMap();
  const originalCreateBufferSource = prototype.createBufferSource;

  function contextRecord(context) {
    let record = contextRecords.get(context);
    if (!record) {
      record = {
        id: probe.contexts.length + 1,
        state: context.state,
        sampleRate: context.sampleRate,
        baseLatencySeconds: context.baseLatency ?? null,
        outputLatencySeconds: context.outputLatency ?? null,
        lastScheduledEndSeconds: null,
        lastQueueWallMs: null,
      };
      contextRecords.set(context, record);
      probe.contexts.push(record);
    }
    record.state = context.state;
    record.sampleRate = context.sampleRate;
    return record;
  }

  window.__jcResetAudioProbe = () => {
    probe.windowGeneration += 1;
    probe.windowStartedAtMs = performance.now();
    probe.windowQueuedBuffers = 0;
    probe.windowEndedBuffers = 0;
    probe.windowFramesQueued = 0;
    probe.windowScheduledSeconds = 0;
    probe.windowPositiveGapCount = 0;
    probe.windowMaxPositiveGapMs = 0;
    probe.windowQueueIntervalCount = 0;
    probe.windowQueueIntervalTotalMs = 0;
    probe.windowMaxQueueIntervalMs = 0;
    probe.windowBufferFramesMin = null;
    probe.windowBufferFramesMax = null;
    probe.contexts.forEach((record) => {
      record.lastScheduledEndSeconds = null;
      record.lastQueueWallMs = null;
    });
    return probe;
  };

  prototype.createBufferSource = function (...createArguments) {
    const context = this;
    const source = originalCreateBufferSource.apply(context, createArguments);
    const originalStart = source.start;
    source.start = function (when = 0, ...startArguments) {
      const record = contextRecord(context);
      const buffer = source.buffer;
      const frames = buffer?.length || 0;
      const sampleRate = buffer?.sampleRate || context.sampleRate || 0;
      const durationSeconds = sampleRate > 0 ? frames / sampleRate : 0;
      const scheduledStartSeconds = Number(when) || context.currentTime;
      const scheduledEndSeconds = scheduledStartSeconds + durationSeconds;
      const wallMs = performance.now();
      const generation = probe.windowGeneration;
      let countedAsEnded = false;
      source.addEventListener(
        "ended",
        () => {
          if (countedAsEnded) {
            probe.endedBuffers += 1;
            if (generation === probe.windowGeneration) {
              probe.windowEndedBuffers += 1;
            }
          }
        },
        { once: true },
      );
      const result = originalStart.call(source, when, ...startArguments);
      countedAsEnded = true;

      probe.queuedBuffers += 1;
      probe.framesQueued += frames;
      probe.scheduledSeconds += durationSeconds;
      probe.windowQueuedBuffers += 1;
      probe.windowFramesQueued += frames;
      probe.windowScheduledSeconds += durationSeconds;
      probe.windowBufferFramesMin =
        probe.windowBufferFramesMin === null
          ? frames
          : Math.min(probe.windowBufferFramesMin, frames);
      probe.windowBufferFramesMax =
        probe.windowBufferFramesMax === null
          ? frames
          : Math.max(probe.windowBufferFramesMax, frames);

      if (record.lastScheduledEndSeconds !== null) {
        const gapMs =
          (scheduledStartSeconds - record.lastScheduledEndSeconds) * 1000;
        if (gapMs > 0.5) {
          probe.windowPositiveGapCount += 1;
          probe.windowMaxPositiveGapMs = Math.max(
            probe.windowMaxPositiveGapMs,
            gapMs,
          );
        }
      }
      if (record.lastQueueWallMs !== null) {
        const intervalMs = wallMs - record.lastQueueWallMs;
        probe.windowQueueIntervalCount += 1;
        probe.windowQueueIntervalTotalMs += intervalMs;
        probe.windowMaxQueueIntervalMs = Math.max(
          probe.windowMaxQueueIntervalMs,
          intervalMs,
        );
      }
      record.lastScheduledEndSeconds = scheduledEndSeconds;
      record.lastQueueWallMs = wallMs;
      return result;
    };
    return source;
  };

  Object.defineProperty(prototype, "__jcAudioSmokeProbeInstalled", {
    value: true,
  });
  window.__jcAudioProbe = probe;
  window.__jcResetAudioProbe();
}

installAudioSmokeProbe();

function installWebGLSmokeProbe() {
  if (!new URLSearchParams(window.location.search).has("smoke")) return;
  if (
    window.__jcWebGLProbe?.installed &&
    typeof window.__jcResetWebGLProbe === "function"
  ) {
    return;
  }

  const canvasPrototype = window.HTMLCanvasElement?.prototype;
  const randomSalt = new Uint32Array(1);
  if (!canvasPrototype || !window.crypto?.getRandomValues) {
    window.__jcWebGLProbe = {
      installed: false,
      error: "WebGL probe prerequisites are unavailable",
    };
    return;
  }
  window.crypto.getRandomValues(randomSalt);

  const probe = {
    installed: true,
    version: 1,
    installedAtMs: performance.now(),
    signatureKind: "ephemeral-salted-sampled-fnv32",
    contextsCreated: 0,
    texImage2DCalls: 0,
    texSubImage2DCalls: 0,
    typedArrayUploads: 0,
    videoUploadCandidates: 0,
    drawArraysCalls: 0,
    drawElementsCalls: 0,
    clearCalls: 0,
    contextLostEvents: 0,
    contextRestoredEvents: 0,
    windowGeneration: 0,
  };
  const seenContexts = new WeakSet();
  let windowUploadSignatures = new Set();

  function increment(field) {
    probe[field] += 1;
    const windowField = `window${field[0].toUpperCase()}${field.slice(1)}`;
    probe[windowField] += 1;
  }

  function recordContext(context, sourceCanvas) {
    if (!context || seenContexts.has(context)) return;
    seenContexts.add(context);
    increment("contextsCreated");
    sourceCanvas.addEventListener("webglcontextlost", () => {
      increment("contextLostEvents");
    });
    sourceCanvas.addEventListener("webglcontextrestored", () => {
      increment("contextRestoredEvents");
    });
  }

  function sampleVideoUpload(methodName, methodArguments) {
    const pixels = methodArguments[methodArguments.length - 1];
    if (!ArrayBuffer.isView(pixels)) return;
    increment("typedArrayUploads");

    const widthIndex = methodName === "texImage2D" ? 3 : 4;
    const heightIndex = methodName === "texImage2D" ? 4 : 5;
    const width = Number(methodArguments[widthIndex]);
    const height = Number(methodArguments[heightIndex]);
    if (!(width >= 320 && height >= 240)) return;
    increment("videoUploadCandidates");

    const bytes = new Uint8Array(
      pixels.buffer,
      pixels.byteOffset,
      pixels.byteLength,
    );
    const sampleCount = Math.min(64, bytes.length);
    let signature = (randomSalt[0] ^ bytes.length) >>> 0;
    for (let index = 0; index < sampleCount; index += 1) {
      const offset =
        sampleCount === 1
          ? 0
          : Math.floor((index * (bytes.length - 1)) / (sampleCount - 1));
      signature = Math.imul(
        signature ^ bytes[offset] ^ (offset & 0xff),
        0x01000193,
      ) >>> 0;
    }
    windowUploadSignatures.add(signature);
    probe.windowDistinctSampledVideoUploads = windowUploadSignatures.size;
    probe.windowSampledUploadBytes += sampleCount;
    probe.windowRollingUploadSignature = Math.imul(
      probe.windowRollingUploadSignature ^ signature,
      0x01000193,
    ) >>> 0;
  }

  function wrapContextMethod(ContextClass, methodName) {
    const contextPrototype = ContextClass?.prototype;
    if (!contextPrototype) return;
    const installedMarker = `__jcSmokeWrapped_${methodName}`;
    if (contextPrototype[installedMarker]) return;
    const original = contextPrototype[methodName];
    if (typeof original !== "function") return;
    contextPrototype[methodName] = function (...methodArguments) {
      const result = original.apply(this, methodArguments);
      increment(`${methodName}Calls`);
      if (methodName === "texImage2D" || methodName === "texSubImage2D") {
        sampleVideoUpload(methodName, methodArguments);
      }
      return result;
    };
    Object.defineProperty(contextPrototype, installedMarker, { value: true });
  }

  window.__jcResetWebGLProbe = () => {
    probe.windowGeneration += 1;
    probe.windowStartedAtMs = performance.now();
    probe.windowContextsCreated = 0;
    probe.windowTexImage2DCalls = 0;
    probe.windowTexSubImage2DCalls = 0;
    probe.windowTypedArrayUploads = 0;
    probe.windowVideoUploadCandidates = 0;
    probe.windowDrawArraysCalls = 0;
    probe.windowDrawElementsCalls = 0;
    probe.windowClearCalls = 0;
    probe.windowContextLostEvents = 0;
    probe.windowContextRestoredEvents = 0;
    probe.windowDistinctSampledVideoUploads = 0;
    probe.windowSampledUploadBytes = 0;
    probe.windowRollingUploadSignature =
      (randomSalt[0] ^ probe.windowGeneration) >>> 0;
    windowUploadSignatures = new Set();
    return probe;
  };

  const originalGetContext = canvasPrototype.getContext;
  canvasPrototype.getContext = function (contextType, ...contextArguments) {
    const context = originalGetContext.call(
      this,
      contextType,
      ...contextArguments,
    );
    if (/^(webgl2?|experimental-webgl)$/.test(String(contextType))) {
      recordContext(context, this);
    }
    return context;
  };

  for (const ContextClass of [
    window.WebGLRenderingContext,
    window.WebGL2RenderingContext,
  ]) {
    for (const methodName of [
      "texImage2D",
      "texSubImage2D",
      "drawArrays",
      "drawElements",
      "clear",
    ]) {
      wrapContextMethod(ContextClass, methodName);
    }
  }

  window.__jcWebGLProbe = probe;
  window.__jcResetWebGLProbe();
}

installWebGLSmokeProbe();

function setStatus(message, isError = false) {
  statusElement.textContent = message;
  statusElement.classList.toggle("error", isError);
}

function selectedPair() {
  const selected = Array.from(contentInput.files || []);
  const map = selected.find((file) => file.name.toUpperCase().endsWith(".MAP"));
  const archive = selected.find((file) => file.name.toUpperCase().endsWith(".001"));
  return map && archive ? { map, archive } : null;
}

function readFile(file) {
  return file.arrayBuffer().then((buffer) => new Uint8Array(buffer));
}

async function localServerPair() {
  const responses = await Promise.all([
    fetch(localContentPaths.map, { cache: "no-store" }),
    fetch(localContentPaths.archive, { cache: "no-store" }),
  ]);
  if (!responses.every((response) => response.ok)) return null;
  const [mapBuffer, archiveBuffer] = await Promise.all(
    responses.map((response) => response.arrayBuffer()),
  );
  const sounds = (
    await Promise.all(
      originalSoundIds.map(async (id) => {
        try {
          const response = await fetch(`local-content/sound${id}.wav`, {
            cache: "no-store",
          });
          if (!response.ok) return null;
          return {
            id,
            name: `local-content/sound${id}.wav`,
            data: new Uint8Array(await response.arrayBuffer()),
          };
        } catch (error) {
          console.warn(`Could not load optional sound${id}.wav:`, error);
          return null;
        }
      }),
    )
  ).filter(Boolean);
  return {
    map: { name: "local-content/RESOURCE.MAP" },
    archive: { name: "local-content/RESOURCE.001" },
    mapData: new Uint8Array(mapBuffer),
    archiveData: new Uint8Array(archiveBuffer),
    sounds,
  };
}

function createZipFileSystem(buffer) {
  return new Promise((resolve, reject) => {
    const zipData = BrowserFS.BFSRequire("buffer").Buffer(new Uint8Array(buffer));
    BrowserFS.FileSystem.ZipFS.Create({ zipData }, (error, fileSystem) => {
      if (error) reject(error);
      else resolve(fileSystem);
    });
  });
}

function modulePreRun(module) {
  module.ENV.LIBRARY_PATH = corePath;
}

async function createModule() {
  const moduleFactory = await import("./johnny_castaway_libretro.js");
  return moduleFactory.default({
    noInitialRun: true,
    arguments: ["-v", mapPath, "-c", configPath],
    preRun: [modulePreRun],
    corePath,
    canvas,
    print: (text) => console.log("RetroArch:", text),
    printErr: (text) => console.error("RetroArch:", text),
    retroArchSend(message) {
      this.EmscriptenSendCommand(message);
    },
    retroArchRecv() {
      return this.EmscriptenReceiveCommandReply();
    },
    retroArchExit() {
      running = false;
      renderAudioState();
      setStatus("RetroArch exited. Reload the page to start again.");
    },
  });
}

function mountBrowserFileSystem(module, zipFileSystem) {
  const mountable = new BrowserFS.FileSystem.MountableFileSystem();
  mountable.mount(retroarchRoot, zipFileSystem);
  mountable.mount(`${retroarchRoot}/cores`, new BrowserFS.FileSystem.InMemory());
  mountable.mount(`${retroarchRoot}/userdata`, new BrowserFS.FileSystem.InMemory());
  BrowserFS.initialize(mountable);

  const emscriptenFileSystem = new BrowserFS.EmscriptenFS(
    module.FS,
    module.PATH,
    module.ERRNO_CODES,
  );
  module.FS.mount(emscriptenFileSystem, { root: "/home" }, "/home");
  module.FS.mkdirTree(contentDirectory);
  module.FS.writeFile(corePath, new Uint8Array());
}

function installContent(module, pair, mapData, archiveData, sounds, coreOptions) {
  // Normalizing names makes the core's sibling-file lookup work even when the
  // user's source filesystem used lowercase names.
  module.FS.writeFile(`${contentDirectory}/RESOURCE.MAP`, mapData);
  module.FS.writeFile(`${contentDirectory}/RESOURCE.001`, archiveData);
  sounds.forEach((sound) => {
    module.FS.writeFile(
      `${contentDirectory}/sound${sound.id}.wav`,
      sound.data,
    );
  });
  module.FS.writeFile(
    configPath,
    [
      `assets_directory = "${retroarchRoot}/assets"`,
      `libretro_info_path = "${retroarchRoot}/info"`,
      `libretro_directory = "${retroarchRoot}/cores"`,
      'menu_driver = "ozone"',
      'video_driver = "gl"',
      'audio_driver = "rwebaudio"',
      'audio_latency = "256"',
      'menu_scroll_delay = "500"',
      "menu_show_load_core = false",
      "menu_show_load_content = false",
      `# Loaded locally from ${pair.map.name} and ${pair.archive.name}`,
      `# Installed ${sounds.length} optional original sound-effect WAVs`,
      "",
    ].join("\n"),
  );
  if (coreOptions) module.FS.writeFile(coreOptionsPath, coreOptions);
}

export async function start(pair = selectedPair(), coreOptions = null) {
  if (!pair || running) return;

  startButton.disabled = true;
  contentInput.disabled = true;
  setStatus("Loading the local RetroArch Web runtime…");

  try {
    const [bundleResponse, mapData, archiveData, module] = await Promise.all([
      fetch("assets/frontend/bundle.zip").then((response) => {
        if (!response.ok) throw new Error(`asset bundle HTTP ${response.status}`);
        return response.arrayBuffer();
      }),
      pair.mapData || readFile(pair.map),
      pair.archiveData || readFile(pair.archive),
      createModule(),
    ]);
    const zipFileSystem = await createZipFileSystem(bundleResponse);
    moduleInstance = module;
    mountBrowserFileSystem(module, zipFileSystem);
    installContent(
      module,
      pair,
      mapData,
      archiveData,
      pair.sounds || [],
      coreOptions,
    );

    running = true;
    renderAudioState();
    menuButton.disabled = false;
    resetButton.disabled = false;
    fullscreenButton.disabled = false;
    const soundStatus = pair.sounds?.length
      ? ` Loaded ${pair.sounds.length} optional sound effects.`
      : "";
    setStatus(`Running.${soundStatus} Use RetroArch menu to inspect core options.`);
    module.callMain(module.arguments);
    canvas.focus();
  } catch (error) {
    console.error(error);
    running = false;
    renderAudioState();
    setStatus(`Could not start: ${error.message}`, true);
    startButton.disabled = false;
    contentInput.disabled = false;
  }
}

if (new URLSearchParams(window.location.search).has("smoke")) {
  window.__jcStartForSmoke = start;
}

async function autoStartLocalContent() {
  setStatus("Checking for locally staged Johnny data…");
  try {
    const pair = await localServerPair();
    if (pair) {
      setStatus("Found locally staged data; starting Johnny…");
      await start(pair);
      return;
    }
  } catch (error) {
    console.warn("Local Johnny data auto-detection failed:", error);
  }
  setStatus("Choose both data files to begin.");
}

function sendCommand(command) {
  if (!running || !moduleInstance) return;
  moduleInstance.retroArchSend(command);
  canvas.focus();
}

contentInput.addEventListener("change", () => {
  const pair = selectedPair();
  startButton.disabled = !pair;
  setStatus(
    pair
      ? `Ready: ${pair.map.name} + ${pair.archive.name}`
      : "Choose one .MAP file and one .001 file.",
    !pair && contentInput.files.length > 0,
  );
});

startButton.addEventListener("click", () => start());
menuButton.addEventListener("click", () => sendCommand("MENU_TOGGLE"));
resetButton.addEventListener("click", () => sendCommand("RESET"));
fullscreenButton.addEventListener("click", () => sendCommand("FULLSCREEN_TOGGLE"));
audioUnlockButton.addEventListener("click", async () => {
  await unlockAudio();
  canvas.focus();
});
document.addEventListener("pointerdown", unlockAudioFromGesture, true);
document.addEventListener("keydown", unlockAudioFromGesture, true);
document.addEventListener("touchstart", unlockAudioFromGesture, {
  capture: true,
  passive: true,
});

autoStartLocalContent();
