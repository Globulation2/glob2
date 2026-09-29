/* Offline gallery controller. Screen IDs are stable feedback keys; user text is
 * always inserted through textContent/value, never interpreted as HTML. */
"use strict";
const data = window.GALLERY;
const $ = (id) => document.getElementById(id);
const key = "glob2-mobile-review-v1";
const states = new Set(["unreviewed", "changes", "approved"]);
let feedback = {};
let storageAvailable = true;
try {
  feedback = JSON.parse(localStorage.getItem(key) || "{}");
} catch {
  storageAvailable = false;
}
let current =
  data.screens.find((s) => s.id === location.hash.slice(1)) || data.screens[0];
let noticeTimer;

function notify(message) {
  $("notice").textContent = message;
  $("notice").style.display = "block";
  clearTimeout(noticeTimer);
  noticeTimer = setTimeout(() => {
    $("notice").style.display = "none";
  }, 3500);
}
function entry(id) {
  return feedback[id] || { status: "unreviewed", notes: "" };
}
function persist() {
  try {
    localStorage.setItem(key, JSON.stringify(feedback));
  } catch {
    storageAvailable = false;
  }
  if (!storageAvailable)
    $("storage-note").textContent =
      "Browser storage is unavailable. Export feedback before closing this page.";
}
function save() {
  feedback[current.id] = { status: $("status").value, notes: $("notes").value };
  persist();
  navigation();
}
function filteredScreens() {
  const query = $("search").value.toLowerCase();
  return data.screens.filter(
    (s) =>
      [s.id, s.title, s.group, s.description]
        .join(" ")
        .toLowerCase()
        .includes(query) &&
      ($("filter").value === "all" || entry(s.id).status === $("filter").value),
  );
}
function navigation() {
  const nav = $("screens");
  nav.replaceChildren();
  let group;
  for (const screen of filteredScreens()) {
    if (screen.group !== group) {
      group = screen.group;
      const heading = document.createElement("h3");
      heading.textContent = group;
      nav.append(heading);
    }
    const link = document.createElement("a");
    link.href = "#" + screen.id;
    link.setAttribute("aria-current", String(current.id === screen.id));
    const marker =
      entry(screen.id).status === "approved"
        ? "✓ "
        : entry(screen.id).status === "changes"
          ? "● "
          : "";
    link.append(marker + screen.title);
    const id = document.createElement("small");
    id.textContent = screen.id;
    link.append(id);
    nav.append(link);
  }
  const reviewed = data.screens.filter(
    (s) => entry(s.id).status !== "unreviewed",
  ).length;
  $("progress").textContent = `${reviewed} / ${data.screens.length} reviewed`;
}
function compare() {
  const holder = $("comparison");
  holder.replaceChildren();
  holder.classList.toggle("actual", $("actual-size").checked);
  const ids = $("all-sizes").checked
    ? data.sizes.map((s) => s.id)
    : [...new Set([$("size-a").value, $("size-b").value])];
  for (const id of ids) {
    const size = data.sizes.find((s) => s.id === id);
    const figure = document.createElement("figure");
    const caption = document.createElement("figcaption");
    caption.append(size.label);
    const dimensions = document.createElement("span");
    dimensions.textContent = `${size.width} × ${size.height}`;
    caption.append(dimensions);
    const stage = document.createElement("div");
    stage.className = "image-stage";
    if (!current.captures[id]) {
      stage.textContent =
        current.availability === "retired"
          ? "Retired — see baseline gallery"
          : `Not applicable · ${current.availability || "unavailable"} view`;
      figure.append(caption, stage);
      holder.append(figure);
      continue;
    }
    const button = document.createElement("button");
    button.setAttribute(
      "aria-label",
      `Enlarge ${current.title}, ${size.label}`,
    );
    const image = document.createElement("img");
    image.src = current.captures[id].src;
    image.alt = `${current.title} — ${size.label}, ${size.width} × ${size.height}`;
    image.width = size.width;
    image.height = size.height;
    button.onclick = () => {
      $("zoom-image").src = image.src;
      $("zoom-image").alt = image.alt;
      $("zoom-title").textContent = `${current.id} · ${image.alt}`;
      $("zoom").showModal();
    };
    button.append(image);
    stage.append(button);
    figure.append(caption, stage);
    const stroke = ["game-zone-paint", "editor-palette-terrain", "editor-palette-resources"].includes(current.id);
    const recordingPath = current.id.startsWith("editor-") ?
      (stroke ? data.editorStrokeRecordings?.[id] : ["editor-map", "editor-tools"].includes(current.id) ? data.editorRecordings?.[id] : null) :
      stroke ? data.strokeRecordings?.[id] :
      ["game-build", "game-placement"].includes(current.id) ? data.recordings?.[id] : null;
    if (recordingPath) {
      const recording = document.createElement("a");
      recording.href = recordingPath;
      recording.target = "_blank";
      recording.rel = "noopener";
      recording.textContent = `Watch ${stroke ? "paint stroke" : "drag placement"} · native SDL input recording`;
      figure.append(recording);
    }
    holder.append(figure);
  }
}
function show(screen) {
  current = screen;
  $("screen-id").textContent = `${screen.group} / ${screen.id}`;
  $("screen-title").textContent = screen.title;
  $("screen-description").textContent = screen.description;
  $("status").value = entry(screen.id).status;
  $("notes").value = entry(screen.id).notes;
  const index = data.screens.indexOf(screen);
  $("previous").disabled = index === 0;
  $("next").disabled = index === data.screens.length - 1;
  navigation();
  compare();
}
function download(name, value) {
  const url = URL.createObjectURL(
    new Blob([JSON.stringify(value, null, 2) + "\n"], {
      type: "application/json",
    }),
  );
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$("summary").textContent =
  `${data.screens.length} named views · ${data.sizes.length} sizes · ${data.screens.length * data.sizes.length} screenshots`;
for (const target of ["size-a", "size-b"])
  for (const size of data.sizes) {
    const option = document.createElement("option");
    option.value = size.id;
    option.textContent = `${size.label} (${size.width} × ${size.height})`;
    $(target).append(option);
  }
$("size-a").value = "phone-portrait";
$("size-b").value = "desktop-laptop";
for (const id of ["size-a", "size-b", "all-sizes", "actual-size"])
  $(id).onchange = compare;
$("search").oninput = navigation;
$("filter").onchange = navigation;
$("status").onchange = save;
$("notes").oninput = save;
$("previous").onclick = () => {
  location.hash = data.screens[data.screens.indexOf(current) - 1].id;
};
$("next").onclick = () => {
  location.hash = data.screens[data.screens.indexOf(current) + 1].id;
};
window.onhashchange = () => {
  const screen = data.screens.find((s) => s.id === location.hash.slice(1));
  if (screen) show(screen);
};
$("export").onclick = () =>
  download("glob2-mobile-feedback.json", {
    schema: 1,
    capturedCommit: data.commit,
    exported: new Date().toISOString(),
    screens: data.screens.map((s) => ({
      id: s.id,
      title: s.title,
      ...entry(s.id),
    })),
  });
$("import").onchange = async (event) => {
  const file = event.target.files[0];
  if (!file) return;
  try {
    const imported = JSON.parse(await file.text());
    if (imported.schema !== 1 || !Array.isArray(imported.screens))
      throw Error("Unsupported feedback format");
    const updates = {};
    for (const row of imported.screens) {
      if (!data.screens.some((s) => s.id === row.id)) continue;
      if (!states.has(row.status) || typeof row.notes !== "string")
        throw Error("Invalid feedback entry");
      // Empty imported records must not erase work already done in this browser.
      if (row.notes || row.status !== "unreviewed")
        updates[row.id] = { status: row.status, notes: row.notes };
    }
    Object.assign(feedback, updates);
    persist();
    show(current);
    notify(`Imported ${Object.keys(updates).length} feedback entries`);
  } catch (error) {
    notify(`Import failed: ${error.message}`);
  }
  event.target.value = "";
};
$("coverage").onclick = () => $("details").showModal();
for (const dialog of document.querySelectorAll("dialog"))
  dialog.querySelector(".close").onclick = () => dialog.close();
const details = $("details-content");
for (const message of [
  data.renderer,
  `Source commit: ${data.commit}${data.dirty ? " (working tree changes included)" : ""}`,
  `Captured: ${data.generated}`,
  "These are real production screens with offline review fixtures. Screenshots preserve layout flaws. They do not establish Android/iOS device behavior, touch feel, safe areas, or onscreen-keyboard behavior.",
  "Each viewport uses a fresh profile. Gameplay uses a seeded, populated match with objectives and recorded history. Inspector replacement IDs preserve earlier feedback. Tablet Automatic/Spacious touch views and desktop mouse views are labeled separately. Building recordings dispatch SDL pointer events through production controls; they are native-host demonstrations, not physical-device recordings. Real keyboards, device safe areas, and play comfort still require Android/iOS validation.",
]) {
  const p = document.createElement("p");
  p.textContent = message;
  details.append(p);
}
const heading = document.createElement("h3");
heading.textContent = "Coverage still requiring another capture path";
details.append(heading);
const list = document.createElement("ul");
for (const gap of data.gaps) {
  const li = document.createElement("li");
  li.textContent = `${gap.title}: ${gap.reason}`;
  list.append(li);
}
details.append(list);
const links = document.createElement("p");
for (const [label, href] of [
  ["Capture manifest", "manifest.json"],
  ["Source diff", "source.patch"],
]) {
  const a = document.createElement("a");
  a.textContent = label;
  a.href = href;
  links.append(a, " · ");
}
details.append(links);
show(current);
persist();
