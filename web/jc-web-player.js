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
    setStatus(`Could not start: ${error.message}`, true);
    startButton.disabled = false;
    contentInput.disabled = false;
  }
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

autoStartLocalContent();
