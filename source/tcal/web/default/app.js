(() => {
  "use strict";

  const appRoot = document.querySelector(".app");
  const monthTitle = document.getElementById("monthTitle");
  const monthGrid = document.getElementById("monthGrid");
  const calendarHeightSplitter = document.getElementById("calendarHeightSplitter");
  const layoutSplitter = document.getElementById("layoutSplitter");
  const dayTitle = document.getElementById("dayTitle");
  const taskList = document.getElementById("taskList");
  const timelineView = document.getElementById("timelineView");
  const taskForm = document.getElementById("taskForm");
  const taskTitle = document.getElementById("taskTitle");
  const viewModeSelect = document.getElementById("viewModeSelect");
  const rangePresetSelect = document.getElementById("rangePresetSelect");
  const customRangeWrap = document.getElementById("customRangeWrap");
  const customRangeDaysInput = document.getElementById("customRangeDays");
  const timelineLayoutWrap = document.getElementById("timelineLayoutWrap");
  const timelineLayoutSelect = document.getElementById("timelineLayoutSelect");
  const timelineHourStartWrap = document.getElementById("timelineHourStartWrap");
  const timelineHourStartInput = document.getElementById("timelineHourStartInput");
  const settingsButton = document.getElementById("settingsButton");

  let cursor = new Date();
  let selectedDate = new Date();
  const inMemoryTasks = new Map();

  const hasHostBridge = !!(window.chrome && window.chrome.webview);
  const pendingRequests = new Map();
  let requestSeq = 0;
  let mutationInFlight = false;
  let loadSeq = 0;
  let isMonthTitleEditing = false;
  let monthTitleEditInput = null;

  const layoutResizeState = {
    active: false,
    startX: 0,
    startRightWidth: 420
  };

  const calendarHeightResizeState = {
    active: false,
    startY: 0,
    startHeight: 420
  };

  let currentViewMode = "list";
  let currentRangePreset = "1";
  let currentCustomRangeDays = 7;
  let currentTimelineLayout = "shared";
  let currentTimelineHourStart = 0;
  let currentLoadedTasks = [];
  let currentUiFontFamily = "Segoe UI";
  let currentUiBaseFontSize = 14;
  let currentUiCalendarDateFontSize = 13;
  let currentUiTaskFontSize = 14;
  let currentUiShowTaskPanel = true;

  const weekdayLabels = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

  const PANEL_RIGHT_MIN = 320;
  const PANEL_LEFT_MIN = 420;
  const SPLITTER_WIDTH = 8;
  const PANEL_RIGHT_STORAGE_KEY = "tcalendar.panelRightWidth";
  const CALENDAR_HEIGHT_STORAGE_KEY = "tcalendar.calendarHeight";
  const TIMELINE_LAYOUT_STORAGE_KEY = "tcalendar.timelineLayout";
  const TIMELINE_HOUR_START_STORAGE_KEY = "tcalendar.timelineHourStart";
  const CALENDAR_HEIGHT_MIN = 280;
  const CALENDAR_HEIGHT_MAX = 1200;

  function keyOf(d) {
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    return `${y}-${m}-${day}`;
  }

  function dateFromKey(dateKey) {
    const parts = (dateKey || "").split("-");
    if (parts.length !== 3) return new Date(NaN);
    const y = Number(parts[0]);
    const m = Number(parts[1]);
    const day = Number(parts[2]);
    if (!Number.isInteger(y) || !Number.isInteger(m) || !Number.isInteger(day)) return new Date(NaN);
    return new Date(y, m - 1, day);
  }

  function addDays(baseDate, offsetDays) {
    return new Date(baseDate.getFullYear(), baseDate.getMonth(), baseDate.getDate() + offsetDays);
  }

  function clampCustomRangeDays(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 7;
    const i = Math.floor(n);
    if (i < 1) return 1;
    if (i > 365) return 365;
    return i;
  }

  function normalizeTimelineLayout(value) {
    return value === "per_task" ? "per_task" : "shared";
  }

  function normalizeTimelineHourStart(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0;
    const i = Math.floor(n);
    if (i < 0) return 0;
    if (i > 23) return 23;
    return i;
  }

  function normalizeUiFontSize(value, fallback) {
    const n = Number(value);
    if (!Number.isFinite(n)) return fallback;
    const i = Math.floor(n);
    if (i < 9) return 9;
    if (i > 28) return 28;
    return i;
  }

  function normalizeUiFontFamily(value) {
    const s = String(value || "").trim();
    return s || "Segoe UI";
  }

  function applyUiStyleConfig() {
    const rootStyle = document.documentElement.style;
    rootStyle.setProperty("--font-family", currentUiFontFamily);
    rootStyle.setProperty("--font-size", `${currentUiBaseFontSize}px`);
    rootStyle.setProperty("--calendar-date-font-size", `${currentUiCalendarDateFontSize}px`);
    rootStyle.setProperty("--task-font-size", `${currentUiTaskFontSize}px`);
  }

  function applyTaskPanelVisibility() {
    if (!appRoot) return;
    document.body.classList.toggle("hideTaskPanel", !currentUiShowTaskPanel);
    if (currentUiShowTaskPanel) {
      applyPanelRightWidth(readSavedPanelRightWidth() ?? 420, false);
      syncLayoutSplitterHeight();
    } else {
      appRoot.style.removeProperty("grid-template-columns");
    }
  }

  function panelRightWidthForConfig() {
    if (!currentUiShowTaskPanel) {
      return clampPanelRightWidth(readSavedPanelRightWidth() ?? 420);
    }
    return clampPanelRightWidth(readCurrentPanelRightWidth());
  }

  function readSavedTimelineLayout() {
    try {
      return normalizeTimelineLayout(window.localStorage.getItem(TIMELINE_LAYOUT_STORAGE_KEY));
    } catch (_) {
      return "shared";
    }
  }

  function saveTimelineLayout(value) {
    try {
      window.localStorage.setItem(TIMELINE_LAYOUT_STORAGE_KEY, normalizeTimelineLayout(value));
    } catch (_) {
    }
  }

  function readSavedTimelineHourStart() {
    try {
      return normalizeTimelineHourStart(window.localStorage.getItem(TIMELINE_HOUR_START_STORAGE_KEY));
    } catch (_) {
      return 0;
    }
  }

  function saveTimelineHourStart(value) {
    try {
      window.localStorage.setItem(TIMELINE_HOUR_START_STORAGE_KEY, String(normalizeTimelineHourStart(value)));
    } catch (_) {
    }
  }

  function readSavedPanelRightWidth() {
    try {
      const raw = window.localStorage.getItem(PANEL_RIGHT_STORAGE_KEY);
      if (!raw) return null;
      const n = Number(raw);
      if (!Number.isFinite(n)) return null;
      return Math.floor(n);
    } catch (_) {
      return null;
    }
  }

  function savePanelRightWidth(width) {
    try {
      window.localStorage.setItem(PANEL_RIGHT_STORAGE_KEY, String(width));
    } catch (_) {
    }
  }

  function readSavedCalendarHeight() {
    try {
      const raw = window.localStorage.getItem(CALENDAR_HEIGHT_STORAGE_KEY);
      if (!raw) return null;
      const n = Number(raw);
      if (!Number.isFinite(n)) return null;
      return Math.floor(n);
    } catch (_) {
      return null;
    }
  }

  function saveCalendarHeight(height) {
    try {
      window.localStorage.setItem(CALENDAR_HEIGHT_STORAGE_KEY, String(height));
    } catch (_) {
    }
  }

  function clampCalendarHeight(height) {
    let h = Number(height);
    if (!Number.isFinite(h)) h = 420;
    h = Math.floor(h);
    if (h < CALENDAR_HEIGHT_MIN) h = CALENDAR_HEIGHT_MIN;
    if (h > CALENDAR_HEIGHT_MAX) h = CALENDAR_HEIGHT_MAX;
    return h;
  }

  function readCurrentCalendarHeight() {
    if (!monthGrid) return 420;
    const n = Math.floor(monthGrid.getBoundingClientRect().height);
    if (Number.isFinite(n) && n > 0) return n;
    return readSavedCalendarHeight() ?? 420;
  }

  function syncLayoutSplitterHeight() {
    if (!layoutSplitter || !monthGrid) return;
    const h = Math.floor(monthGrid.getBoundingClientRect().height);
    if (!Number.isFinite(h) || h <= 0) return;
    layoutSplitter.style.height = `${h}px`;
  }

  function applyCalendarHeight(height, persist) {
    if (!monthGrid) return;
    const next = clampCalendarHeight(height);
    monthGrid.style.height = `${next}px`;
    syncLayoutSplitterHeight();
    if (persist) saveCalendarHeight(next);
  }

  function initializeCalendarHeightSplitter() {
    if (!calendarHeightSplitter || !monthGrid) return;

    const initial = readSavedCalendarHeight();
    if (initial != null) {
      applyCalendarHeight(initial, false);
    }

    const onMouseMove = (e) => {
      if (!calendarHeightResizeState.active) return;
      const delta = e.clientY - calendarHeightResizeState.startY;
      const next = calendarHeightResizeState.startHeight + delta;
      applyCalendarHeight(next, false);
    };

    const onMouseUp = () => {
      if (!calendarHeightResizeState.active) return;
      calendarHeightResizeState.active = false;
      document.body.classList.remove("isResizingCalendarHeight");
      applyCalendarHeight(readCurrentCalendarHeight(), true);
      void saveViewConfigToIni();
    };

    calendarHeightSplitter.addEventListener("mousedown", (e) => {
      e.preventDefault();
      calendarHeightResizeState.active = true;
      calendarHeightResizeState.startY = e.clientY;
      calendarHeightResizeState.startHeight = readCurrentCalendarHeight();
      document.body.classList.add("isResizingCalendarHeight");
    });

    window.addEventListener("mousemove", onMouseMove);
    window.addEventListener("mouseup", onMouseUp);
  }

  function isNarrowLayout() {
    return window.matchMedia("(max-width: 900px)").matches;
  }

  function clampPanelRightWidth(width) {
    const appWidth = appRoot ? appRoot.getBoundingClientRect().width : window.innerWidth;
    const maxRight = Math.max(PANEL_RIGHT_MIN, Math.floor(appWidth - PANEL_LEFT_MIN - SPLITTER_WIDTH));
    let w = Number(width);
    if (!Number.isFinite(w)) w = 420;
    w = Math.floor(w);
    if (w < PANEL_RIGHT_MIN) w = PANEL_RIGHT_MIN;
    if (w > maxRight) w = maxRight;
    return w;
  }

  function applyPanelRightWidth(width, persist) {
    if (!appRoot) return;
    if (isNarrowLayout()) {
      appRoot.style.removeProperty("grid-template-columns");
      return;
    }
    const next = clampPanelRightWidth(width);
    appRoot.style.gridTemplateColumns = `minmax(0, 1fr) ${SPLITTER_WIDTH}px ${next}px`;
    if (persist) savePanelRightWidth(next);
  }

  function initializePanelSplitter() {
    if (!layoutSplitter || !appRoot) return;

    const initial = readSavedPanelRightWidth() ?? 420;
    applyPanelRightWidth(initial, false);
    syncLayoutSplitterHeight();

    const onMouseMove = (e) => {
      if (!layoutResizeState.active) return;
      const delta = e.clientX - layoutResizeState.startX;
      const next = layoutResizeState.startRightWidth - delta;
      applyPanelRightWidth(next, false);
      syncLayoutSplitterHeight();
    };

    const onMouseUp = () => {
      if (!layoutResizeState.active) return;
      layoutResizeState.active = false;
      document.body.classList.remove("isResizingPanels");
      const current = clampPanelRightWidth(readCurrentPanelRightWidth());
      applyPanelRightWidth(current, true);
      syncLayoutSplitterHeight();
      void saveViewConfigToIni();
    };

    layoutSplitter.addEventListener("mousedown", (e) => {
      if (isNarrowLayout()) return;
      e.preventDefault();
      layoutResizeState.active = true;
      layoutResizeState.startX = e.clientX;
      layoutResizeState.startRightWidth = readCurrentPanelRightWidth();
      document.body.classList.add("isResizingPanels");
    });

    window.addEventListener("mousemove", onMouseMove);
    window.addEventListener("mouseup", onMouseUp);
    window.addEventListener("resize", () => {
      applyPanelRightWidth(readCurrentPanelRightWidth(), false);
      syncLayoutSplitterHeight();
    });
  }

  function readCurrentPanelRightWidth() {
    if (!appRoot) return 420;
    const computed = window.getComputedStyle(appRoot).gridTemplateColumns;
    const parts = computed.split(" ");
    const last = parts[parts.length - 1] || "";
    const n = Number(String(last).replace("px", ""));
    if (Number.isFinite(n) && n > 0) return Math.floor(n);
    return readSavedPanelRightWidth() ?? 420;
  }

  function sameDay(a, b) {
    return a.getFullYear() === b.getFullYear() &&
      a.getMonth() === b.getMonth() &&
      a.getDate() === b.getDate();
  }

  function nextRequestId() {
    requestSeq += 1;
    return `web-${requestSeq}`;
  }

  function normalizeTask(raw) {
    return {
      id: raw?.id || "",
      date: raw?.date || "",
      title: raw?.title || "",
      detail: raw?.detail || "",
      startTime: raw?.startTime || "",
      endTime: raw?.endTime || "",
      done: !!raw?.done
    };
  }

  function formatTaskTooltip(task) {
    const lines = [];
    if (task.startTime || task.endTime) {
      if (task.startTime && task.endTime) {
        lines.push(`${task.startTime} - ${task.endTime}`);
      } else if (task.startTime) {
        lines.push(`Start: ${task.startTime}`);
      }
    }
    if (task.detail) {
      lines.push(task.detail);
    }
    return lines.join("\n");
  }

  function compareTasksForList(a, b) {
    const ad = (a.date || "").trim();
    const bd = (b.date || "").trim();
    if (ad !== bd) return ad.localeCompare(bd);

    const at = (a.startTime || "").trim();
    const bt = (b.startTime || "").trim();
    if (at && bt && at !== bt) return at.localeCompare(bt);
    if (!at && bt) return 1;
    if (at && !bt) return -1;

    return (a.title || "").localeCompare(b.title || "");
  }

  function renderTaskRowLabel(target, task) {
    const fullDateText = task.date || keyOf(selectedDate);
    const dateText = /^\d{4}-\d{2}-\d{2}$/.test(fullDateText)
      ? fullDateText.slice(5)
      : fullDateText;
    const startText = task.startTime || "";
    const titleText = task.title || "(untitled)";

    target.textContent = "";

    const datePart = document.createElement("span");
    datePart.className = "taskMetaDate";
    datePart.textContent = dateText;

    const sep1 = document.createElement("span");
    sep1.className = "taskMetaSep";
    sep1.textContent = " : ";

    const timePart = document.createElement("span");
    timePart.className = "taskMetaTime";
    timePart.textContent = startText;

    const gap = document.createElement("span");
    gap.className = "taskMetaGap";
    gap.textContent = "";

    const titlePart = document.createElement("span");
    titlePart.className = "taskMetaMain";
    titlePart.textContent = titleText;

    target.appendChild(datePart);
    target.appendChild(sep1);
    target.appendChild(timePart);
    target.appendChild(gap);
    target.appendChild(titlePart);
  }

  function validateTimeRange(startTime, endTime) {
    if (!startTime && !endTime) return { ok: true, message: "" };
    if (!startTime && endTime) return { ok: false, message: "End time requires start time." };
    if (!/^\d{2}:\d{2}$/.test(startTime)) return { ok: false, message: "Invalid start time format." };
    if (endTime && !/^\d{2}:\d{2}$/.test(endTime)) return { ok: false, message: "Invalid end time format." };
    if (endTime && endTime < startTime) return { ok: false, message: "End time must be after start time." };
    return { ok: true, message: "" };
  }

  function parseTimeToMinutes(value) {
    if (!/^\d{2}:\d{2}$/.test(value || "")) return null;
    const hh = Number(value.slice(0, 2));
    const mm = Number(value.slice(3, 5));
    if (!Number.isInteger(hh) || !Number.isInteger(mm)) return null;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return null;
    return hh * 60 + mm;
  }

  function buildCurrentRange() {
    const fromKey = keyOf(selectedDate);
    const fromDate = dateFromKey(fromKey);

    const preset = currentRangePreset;
    let days = 1;
    if (preset === "custom") {
      days = clampCustomRangeDays(currentCustomRangeDays);
    } else {
      const p = Number(preset);
      days = (p === 1 || p === 7 || p === 14 || p === 30) ? p : 1;
    }

    const toDate = addDays(fromDate, days - 1);
    return {
      fromKey,
      toKey: keyOf(toDate),
      days
    };
  }

  function updateRangeControlVisibility() {
    const isCustom = currentRangePreset === "custom";
    customRangeWrap.classList.toggle("isHidden", !isCustom);
  }

  function setViewMode(mode) {
    currentViewMode = mode === "timeline" ? "timeline" : "list";
    viewModeSelect.value = currentViewMode;
    updateTimelineLayoutControlVisibility();
  }

  function setRangePreset(preset) {
    const allowed = ["1", "7", "14", "30", "custom"];
    currentRangePreset = allowed.includes(String(preset)) ? String(preset) : "1";
    rangePresetSelect.value = currentRangePreset;
    updateRangeControlVisibility();
  }

  function setCustomRangeDays(days) {
    currentCustomRangeDays = clampCustomRangeDays(days);
    customRangeDaysInput.value = String(currentCustomRangeDays);
  }

  function updateTimelineLayoutControlVisibility() {
    const show = currentViewMode === "timeline";
    if (timelineLayoutWrap) {
      timelineLayoutWrap.classList.toggle("isHidden", !show);
    }
    if (timelineHourStartWrap) {
      timelineHourStartWrap.classList.toggle("isHidden", !show);
    }
  }

  function setTimelineLayout(value, persist) {
    currentTimelineLayout = normalizeTimelineLayout(value);
    if (timelineLayoutSelect) {
      timelineLayoutSelect.value = currentTimelineLayout;
    }
    if (persist) {
      saveTimelineLayout(currentTimelineLayout);
    }
  }

  function setTimelineHourStart(value, persist) {
    currentTimelineHourStart = normalizeTimelineHourStart(value);
    if (timelineHourStartInput) {
      timelineHourStartInput.value = String(currentTimelineHourStart);
    }
    if (persist) {
      saveTimelineHourStart(currentTimelineHourStart);
    }
  }

  async function openTaskDetailDialog(initialTask) {
    return new Promise((resolve) => {
      const overlay = document.createElement("div");
      overlay.className = "taskModalOverlay";

      const modal = document.createElement("div");
      modal.className = "taskModal";

      const title = document.createElement("h3");
      title.textContent = "Task Detail";

      const startLabel = document.createElement("label");
      startLabel.textContent = "Start time";
      const startInput = document.createElement("input");
      startInput.type = "time";
      startInput.value = initialTask?.startTime || "";

      const endLabel = document.createElement("label");
      endLabel.textContent = "End time";
      const endInput = document.createElement("input");
      endInput.type = "time";
      endInput.value = initialTask?.endTime || "";

      const detailLabel = document.createElement("label");
      detailLabel.textContent = "Detail";
      const detailInput = document.createElement("textarea");
      detailInput.rows = 6;
      detailInput.value = initialTask?.detail || "";

      const actions = document.createElement("div");
      actions.className = "taskModalActions";
      const cancelButton = document.createElement("button");
      cancelButton.type = "button";
      cancelButton.textContent = "Cancel";
      const saveButton = document.createElement("button");
      saveButton.type = "button";
      saveButton.textContent = "Save";

      actions.appendChild(cancelButton);
      actions.appendChild(saveButton);

      modal.appendChild(title);
      modal.appendChild(startLabel);
      modal.appendChild(startInput);
      modal.appendChild(endLabel);
      modal.appendChild(endInput);
      modal.appendChild(detailLabel);
      modal.appendChild(detailInput);
      modal.appendChild(actions);
      overlay.appendChild(modal);
      document.body.appendChild(overlay);

      const close = (value) => {
        overlay.remove();
        resolve(value);
      };

      cancelButton.addEventListener("click", () => close(null));
      overlay.addEventListener("click", (e) => {
        if (e.target === overlay) close(null);
      });
      saveButton.addEventListener("click", () => {
        const startTime = startInput.value.trim();
        const endTime = endInput.value.trim();
        const detail = detailInput.value.trim();
        const timeCheck = validateTimeRange(startTime, endTime);
        if (!timeCheck.ok) {
          window.alert(timeCheck.message);
          return;
        }
        close({ detail, startTime, endTime });
      });
    });
  }

  function hostCall(method, params) {
    if (!hasHostBridge) {
      return Promise.reject(new Error("HOST_BRIDGE_UNAVAILABLE"));
    }

    const requestId = nextRequestId();
    const payload = {
      apiVersion: "1.0",
      requestId,
      method,
      params: params || {}
    };

    return new Promise((resolve, reject) => {
      const timeoutId = setTimeout(() => {
        pendingRequests.delete(requestId);
        reject(new Error("HOST_TIMEOUT"));
      }, 5000);

      pendingRequests.set(requestId, { resolve, reject, timeoutId });
      window.chrome.webview.postMessage(JSON.stringify(payload));
    });
  }

  if (hasHostBridge) {
    window.chrome.webview.addEventListener("message", (event) => {
      let response = event.data;
      if (typeof response === "string") {
        try {
          response = JSON.parse(response);
        } catch (_) {
          return;
        }
      }

      if (!response || typeof response !== "object" || !response.requestId) return;

      const pending = pendingRequests.get(response.requestId);
      if (!pending) return;

      clearTimeout(pending.timeoutId);
      pendingRequests.delete(response.requestId);

      if (response.ok) {
        pending.resolve(response);
      } else {
        pending.reject(new Error(response.message || response.code || "HOST_ERROR"));
      }
    });
  }

  function refreshBusyUiState() {
    const controls = [taskTitle, viewModeSelect, rangePresetSelect, customRangeDaysInput, timelineLayoutSelect, timelineHourStartInput, settingsButton];
    controls.forEach((el) => { if (el) el.disabled = mutationInFlight; });

    const submitButton = taskForm.querySelector('button[type="submit"]');
    if (submitButton) submitButton.disabled = mutationInFlight;

    const rowControls = document.querySelectorAll("#taskList input, #taskList button, #timelineView input, #timelineView button");
    rowControls.forEach((el) => {
      el.disabled = mutationInFlight;
    });
  }

  async function withTaskMutation(run) {
    if (mutationInFlight) return false;
    mutationInFlight = true;
    refreshBusyUiState();
    try {
      await run();
      return true;
    } finally {
      mutationInFlight = false;
      refreshBusyUiState();
    }
  }

  function updateInMemoryTaskById(id, updater) {
    for (const [dateKey, items] of inMemoryTasks.entries()) {
      const target = items.find((item) => item.id === id);
      if (target) {
        updater(target, dateKey);
        return true;
      }
    }
    return false;
  }

  function deleteInMemoryTaskById(id) {
    let deleted = false;
    for (const [dateKey, items] of inMemoryTasks.entries()) {
      const next = items.filter((item) => item.id !== id);
      if (next.length !== items.length) {
        inMemoryTasks.set(dateKey, next);
        deleted = true;
      }
    }
    return deleted;
  }

  function createTaskActionButtons(t) {
    const actions = document.createElement("div");
    actions.className = "taskActionGroup";

    const editTitle = document.createElement("button");
    editTitle.type = "button";
    editTitle.className = "taskEdit";
    editTitle.textContent = "Title";
    editTitle.addEventListener("click", async () => {
      const nextTitleRaw = window.prompt("Edit task title", t.title || "");
      if (nextTitleRaw === null) return;
      const nextTitle = nextTitleRaw.trim();
      if (!nextTitle) return;

      await withTaskMutation(async () => {
        if (hasHostBridge && t.id) {
          await hostCall("task.updateTitle", { id: t.id, title: nextTitle });
        } else {
          updateInMemoryTaskById(t.id, (target) => {
            target.title = nextTitle;
          });
        }
        await loadTasksForCurrentRange();
      });
    });

    const editDetail = document.createElement("button");
    editDetail.type = "button";
    editDetail.className = "taskEdit";
    editDetail.textContent = "Detail";
    editDetail.addEventListener("click", async () => {
      const edited = await openTaskDetailDialog(t);
      if (!edited) return;

      await withTaskMutation(async () => {
        if (hasHostBridge && t.id) {
          await hostCall("task.update", {
            id: t.id,
            title: t.title,
            detail: edited.detail,
            startTime: edited.startTime,
            endTime: edited.endTime
          });
        } else {
          updateInMemoryTaskById(t.id, (target) => {
            target.detail = edited.detail;
            target.startTime = edited.startTime;
            target.endTime = edited.endTime;
          });
        }
        await loadTasksForCurrentRange();
      });
    });

    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "taskDelete";
    remove.textContent = "Delete";
    remove.addEventListener("click", async () => {
      if (!window.confirm(`Delete task "${t.title}"?`)) return;
      await withTaskMutation(async () => {
        if (hasHostBridge && t.id) {
          await hostCall("task.delete", { id: t.id });
        } else {
          deleteInMemoryTaskById(t.id);
        }
        await loadTasksForCurrentRange();
      });
    });

    actions.appendChild(editTitle);
    actions.appendChild(editDetail);
    actions.appendChild(remove);
    return actions;
  }

  function renderTimelineTaskLabel(target, task) {
    const timeText = task.startTime || "";
    const titleText = task.title || "(untitled)";

    target.textContent = "";

    const timePart = document.createElement("span");
    timePart.className = "timelineTaskTime";
    timePart.textContent = timeText;

    const gapPart = document.createElement("span");
    gapPart.className = "timelineTaskGap";
    gapPart.textContent = "";

    const titlePart = document.createElement("span");
    titlePart.className = "timelineTaskTitle";
    titlePart.textContent = titleText;

    target.appendChild(timePart);
    target.appendChild(gapPart);
    target.appendChild(titlePart);
  }

  function renderTaskList(tasks) {
    taskList.innerHTML = "";

    const sortedTasks = tasks
      .map(normalizeTask)
      .sort(compareTasksForList);

    for (const t of sortedTasks) {
      const li = document.createElement("li");
      const row = document.createElement("label");
      row.className = "taskRow";

      const title = document.createElement("span");
      title.className = "taskTitle";
      renderTaskRowLabel(title, t);
      if (t.done) title.classList.add("isDone");
      const tooltipText = formatTaskTooltip(t);
      if (tooltipText) {
        title.title = tooltipText;
        row.title = tooltipText;
      }

      const editTitle = document.createElement("button");
      editTitle.type = "button";
      editTitle.className = "taskEdit";
      editTitle.textContent = "Title";
      editTitle.addEventListener("click", async () => {
        const nextTitleRaw = window.prompt("Edit task title", t.title || "");
        if (nextTitleRaw === null) return;
        const nextTitle = nextTitleRaw.trim();
        if (!nextTitle) return;

        await withTaskMutation(async () => {
          if (hasHostBridge && t.id) {
            await hostCall("task.updateTitle", { id: t.id, title: nextTitle });
          } else {
            updateInMemoryTaskById(t.id, (target) => {
              target.title = nextTitle;
            });
          }
          await loadTasksForCurrentRange();
        });
      });

      const editDetail = document.createElement("button");
      editDetail.type = "button";
      editDetail.className = "taskEdit";
      editDetail.textContent = "Detail";
      editDetail.addEventListener("click", async () => {
        const edited = await openTaskDetailDialog(t);
        if (!edited) return;

        await withTaskMutation(async () => {
          if (hasHostBridge && t.id) {
            await hostCall("task.update", {
              id: t.id,
              title: t.title,
              detail: edited.detail,
              startTime: edited.startTime,
              endTime: edited.endTime
            });
          } else {
            updateInMemoryTaskById(t.id, (target) => {
              target.detail = edited.detail;
              target.startTime = edited.startTime;
              target.endTime = edited.endTime;
            });
          }
          await loadTasksForCurrentRange();
        });
      });

      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "taskDelete";
      remove.textContent = "Delete";
      remove.addEventListener("click", async () => {
        if (!window.confirm(`Delete task "${t.title}"?`)) return;
        await withTaskMutation(async () => {
          if (hasHostBridge && t.id) {
            await hostCall("task.delete", { id: t.id });
          } else {
            deleteInMemoryTaskById(t.id);
          }
          await loadTasksForCurrentRange();
        });
      });

      row.appendChild(title);
      row.appendChild(editTitle);
      row.appendChild(editDetail);
      row.appendChild(remove);
      li.appendChild(row);
      taskList.appendChild(li);
    }

    if (!tasks.length) {
      const li = document.createElement("li");
      li.className = "taskEmpty";
      li.textContent = "No tasks";
      taskList.appendChild(li);
    }

    refreshBusyUiState();
  }

  function buildTimelineHourAxis(startHour) {
    const axis = document.createElement("div");
    axis.className = "timelineHourAxis";
    for (let offset = 0; offset <= 24; offset += 2) {
      const labelHour = (startHour + offset) % 24;
      const label = document.createElement("span");
      label.textContent = String(labelHour).padStart(2, "0");
      axis.appendChild(label);
    }
    return axis;
  }

  function minuteToAxisPercent(minute, startHour) {
    const origin = startHour * 60;
    const relative = (minute - origin + 1440) % 1440;
    return (relative / 1440) * 100;
  }

  function appendTimelineVisual(bars, task, startHour, compactTrack) {
    const startMin = parseTimeToMinutes(task.startTime);
    if (startMin == null) return;

    const tooltipText = formatTaskTooltip(task) || task.title;
    const endMin = parseTimeToMinutes(task.endTime);
    if (endMin != null && endMin >= startMin) {
      const duration = endMin - startMin;
      const startPct = minuteToAxisPercent(startMin, startHour);
      const widthPct = Math.max((duration / 1440) * 100, 0.9);

      if (startPct + widthPct <= 100) {
        const bar = document.createElement("div");
        bar.className = "timelineBar";
        bar.style.left = `${startPct}%`;
        bar.style.width = `${widthPct}%`;
        bar.title = tooltipText || "";
        if (!compactTrack) {
          bar.textContent = task.title || "(untitled)";
        }
        bars.appendChild(bar);
      } else {
        const firstWidth = Math.max(100 - startPct, 0.9);
        const bar1 = document.createElement("div");
        bar1.className = "timelineBar";
        bar1.style.left = `${startPct}%`;
        bar1.style.width = `${firstWidth}%`;
        bar1.title = tooltipText || "";
        if (!compactTrack) {
          bar1.textContent = task.title || "(untitled)";
        }
        bars.appendChild(bar1);

        const restWidth = Math.max(widthPct - firstWidth, 0.9);
        const bar2 = document.createElement("div");
        bar2.className = "timelineBar";
        bar2.style.left = `0%`;
        bar2.style.width = `${restWidth}%`;
        bar2.title = tooltipText || "";
        bars.appendChild(bar2);
      }
      return;
    }

    const point = document.createElement("div");
    point.className = "timelinePoint";
    point.style.left = `${minuteToAxisPercent(startMin, startHour)}%`;
    point.title = tooltipText || "";
    point.textContent = task.title || "(untitled)";
    bars.appendChild(point);
  }

  function renderTimelineSharedDay(dayBox, dayTasks) {
    const timed = dayTasks.filter((t) => !!t.startTime);
    const untimed = dayTasks.filter((t) => !t.startTime);

    if (timed.length) {
      dayBox.appendChild(buildTimelineHourAxis(currentTimelineHourStart));

      const track = document.createElement("div");
      track.className = "timelineTrack";

      const bars = document.createElement("div");
      bars.className = "timelineBars";

      for (const t of timed) {
        appendTimelineVisual(bars, t, currentTimelineHourStart, false);
      }

      track.appendChild(bars);
      dayBox.appendChild(track);
    }

    if (untimed.length) {
      const untimedWrap = document.createElement("div");
      untimedWrap.className = "timelineUntimed";

      const label = document.createElement("div");
      label.textContent = "Untimed";
      untimedWrap.appendChild(label);

      for (const t of untimed) {
        const item = document.createElement("div");
        item.className = "timelineUntimedItem";
        item.textContent = t.title || "(untitled)";
        item.title = formatTaskTooltip(t) || "";
        untimedWrap.appendChild(item);
      }

      dayBox.appendChild(untimedWrap);
    }

    if (dayTasks.length) {
      const taskListBlock = document.createElement("div");
      taskListBlock.className = "timelineTaskList";

      for (const t of dayTasks) {
        const row = document.createElement("div");
        row.className = "timelineTaskRow";

        const label = document.createElement("span");
        label.className = "timelineTaskLabel";
        renderTimelineTaskLabel(label, t);

        const tooltipText = formatTaskTooltip(t);
        if (tooltipText) {
          label.title = tooltipText;
          row.title = tooltipText;
        }

        row.appendChild(label);
        row.appendChild(createTaskActionButtons(t));
        taskListBlock.appendChild(row);
      }

      dayBox.appendChild(taskListBlock);
    }
  }

  function renderTimelinePerTaskDay(dayBox, dayTasks) {
    const timed = dayTasks.filter((t) => !!t.startTime);
    const untimed = dayTasks.filter((t) => !t.startTime);

    if (untimed.length) {
      const untimedWrap = document.createElement("div");
      untimedWrap.className = "timelineUntimed";

      for (const t of untimed) {
        const row = document.createElement("div");
        row.className = "timelineTaskRow";

        const taskLabel = document.createElement("span");
        taskLabel.className = "timelineTaskLabel";
        renderTimelineTaskLabel(taskLabel, t);

        const tooltipText = formatTaskTooltip(t);
        if (tooltipText) {
          taskLabel.title = tooltipText;
          row.title = tooltipText;
        }

        row.appendChild(taskLabel);
        row.appendChild(createTaskActionButtons(t));
        untimedWrap.appendChild(row);
      }

      dayBox.appendChild(untimedWrap);
    }

    if (timed.length) {
      dayBox.appendChild(buildTimelineHourAxis(currentTimelineHourStart));

      const perTaskList = document.createElement("div");
      perTaskList.className = "timelinePerTaskList";

      for (const t of timed) {
        const item = document.createElement("div");
        item.className = "timelinePerTaskItem";

        const top = document.createElement("div");
        top.className = "timelinePerTaskTop";

        const label = document.createElement("span");
        label.className = "timelineTaskLabel";
        renderTimelineTaskLabel(label, t);

        const tooltipText = formatTaskTooltip(t);
        if (tooltipText) {
          label.title = tooltipText;
          top.title = tooltipText;
        }

        top.appendChild(label);
        top.appendChild(createTaskActionButtons(t));

        const track = document.createElement("div");
        track.className = "timelinePerTaskTrack";
        const bars = document.createElement("div");
        bars.className = "timelineBars";

        appendTimelineVisual(bars, t, currentTimelineHourStart, true);

        track.appendChild(bars);
        item.appendChild(top);
        item.appendChild(track);
        perTaskList.appendChild(item);
      }

      dayBox.appendChild(perTaskList);
    }
  }

  function renderTimelineView(tasks, range) {
    timelineView.innerHTML = "";

    const byDate = new Map();
    for (let i = 0; i < range.days; i += 1) {
      const d = addDays(selectedDate, i);
      byDate.set(keyOf(d), []);
    }
    for (const raw of tasks) {
      const t = normalizeTask(raw);
      if (!byDate.has(t.date)) continue;
      byDate.get(t.date).push(t);
    }

    for (const [dateKey, dayTasks] of byDate.entries()) {
      dayTasks.sort(compareTasksForList);

      const dayBox = document.createElement("div");
      dayBox.className = "timelineDay";

      const header = document.createElement("div");
      header.className = "timelineDayHeader";
      header.textContent = dateKey;
      dayBox.appendChild(header);

      if (currentTimelineLayout === "per_task") {
        renderTimelinePerTaskDay(dayBox, dayTasks);
      } else {
        renderTimelineSharedDay(dayBox, dayTasks);
      }

      if (!dayTasks.length) {
        const empty = document.createElement("div");
        empty.className = "taskEmpty";
        empty.textContent = "No tasks";
        dayBox.appendChild(empty);
      }

      timelineView.appendChild(dayBox);
    }

    refreshBusyUiState();
  }

  function renderCurrentView(range) {
    if (currentViewMode === "timeline") {
      taskList.classList.add("isHidden");
      timelineView.classList.remove("isHidden");
      renderTimelineView(currentLoadedTasks, range);
    } else {
      timelineView.classList.add("isHidden");
      taskList.classList.remove("isHidden");
      renderTaskList(currentLoadedTasks);
    }

    const rangeLabel = range.days === 1
      ? range.fromKey
      : `${range.fromKey} .. ${range.toKey}`;
    dayTitle.textContent = `${currentViewMode === "timeline" ? "Timeline" : "Tasks"} ${rangeLabel}`;
  }

  async function fetchTasksForRange(range) {
    if (hasHostBridge) {
      try {
        const response = await hostCall("calendar.getRangeTasks", {
          dateFrom: range.fromKey,
          dateTo: range.toKey
        });
        const items = response?.data?.items;
        return Array.isArray(items) ? items : [];
      } catch (_) {
        try {
          if (range.days === 1) {
            const fallback = await hostCall("calendar.getDayTasks", { date: range.fromKey });
            const items = fallback?.data?.items;
            return Array.isArray(items) ? items : [];
          }
        } catch (_) {
        }
      }
    }

    const out = [];
    for (let i = 0; i < range.days; i += 1) {
      const dateKey = keyOf(addDays(selectedDate, i));
      const items = inMemoryTasks.get(dateKey) || [];
      for (const item of items) {
        out.push({ ...item, date: item.date || dateKey });
      }
    }
    return out;
  }

  async function loadTasksForCurrentRange() {
    const currentSeq = ++loadSeq;
    const range = buildCurrentRange();

    const items = await fetchTasksForRange(range);
    if (currentSeq !== loadSeq) return;

    currentLoadedTasks = items.map(normalizeTask).sort(compareTasksForList);
    renderCurrentView(range);
  }

  function createDayCell(date, inCurrentMonth) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "dayCell";
    if (!inCurrentMonth) button.classList.add("isOutside");
    if (sameDay(date, selectedDate)) button.classList.add("isSelected");

    const dateLabel = document.createElement("span");
    dateLabel.className = "dateLabel";
    const dayOfWeek = date.getDay();
    if (dayOfWeek === 0) {
      dateLabel.classList.add("isSunday");
    } else if (dayOfWeek === 6) {
      dateLabel.classList.add("isSaturday");
    }
    dateLabel.textContent = String(date.getDate());
    button.appendChild(dateLabel);

    button.addEventListener("click", () => {
      selectedDate = new Date(date.getFullYear(), date.getMonth(), date.getDate());
      if (selectedDate.getFullYear() !== cursor.getFullYear() || selectedDate.getMonth() !== cursor.getMonth()) {
        cursor = new Date(selectedDate.getFullYear(), selectedDate.getMonth(), 1);
      }
      renderMonth();
    });

    return button;
  }

  function renderMonthGrid() {
    monthGrid.innerHTML = "";

    const weekdayRow = document.createElement("div");
    weekdayRow.className = "weekdayRow";
    for (const label of weekdayLabels) {
      const cell = document.createElement("div");
      cell.className = "weekdayCell";
      cell.textContent = label;
      weekdayRow.appendChild(cell);
    }
    monthGrid.appendChild(weekdayRow);

    const daysGrid = document.createElement("div");
    daysGrid.className = "daysGrid";

    const year = cursor.getFullYear();
    const month = cursor.getMonth();
    const firstDay = new Date(year, month, 1);
    const firstWeekday = firstDay.getDay();

    for (let i = 0; i < 42; i += 1) {
      const offset = i - firstWeekday;
      const d = new Date(year, month, 1 + offset);
      const inCurrentMonth = d.getMonth() === month;
      daysGrid.appendChild(createDayCell(d, inCurrentMonth));
    }

    monthGrid.appendChild(daysGrid);
  }

  function currentYearMonthText() {
    return `${cursor.getFullYear()}-${String(cursor.getMonth() + 1).padStart(2, "0")}`;
  }

  function renderMonthTitle() {
    if (isMonthTitleEditing) return;
    monthTitle.innerHTML = "";
    const label = document.createElement("span");
    label.className = "monthTitleText";
    label.textContent = currentYearMonthText();
    monthTitle.appendChild(label);
  }

  async function openSettingsDialog() {
    return new Promise((resolve) => {
      const snapshot = {
        uiFontFamily: currentUiFontFamily,
        uiBaseFontSize: currentUiBaseFontSize,
        uiCalendarDateFontSize: currentUiCalendarDateFontSize,
        uiTaskFontSize: currentUiTaskFontSize,
        uiShowTaskPanel: currentUiShowTaskPanel
      };

      const overlay = document.createElement("div");
      overlay.className = "taskModalOverlay";

      const modal = document.createElement("div");
      modal.className = "taskModal settingsModal";

      const header = document.createElement("div");
      header.className = "settingsHeader";
      const title = document.createElement("h3");
      title.textContent = "Settings";
      const intro = document.createElement("p");
      intro.className = "settingsIntro";
      intro.textContent = "Preview is applied immediately. Apply saves to INI. Cancel restores previous values.";
      header.appendChild(title);
      header.appendChild(intro);

      const body = document.createElement("div");
      body.className = "settingsBody";

      const createNumberInput = (value, min, max) => {
        const input = document.createElement("input");
        input.type = "number";
        input.min = String(min);
        input.max = String(max);
        input.value = String(value);
        return input;
      };

      const createSection = (sectionTitle) => {
        const section = document.createElement("section");
        section.className = "settingsSection";
        const h = document.createElement("h4");
        h.textContent = sectionTitle;
        section.appendChild(h);
        return section;
      };

      const createRow = (labelText, controlEl, hintText) => {
        const row = document.createElement("label");
        row.className = "settingsRow";

        const label = document.createElement("span");
        label.className = "settingsLabel";
        label.textContent = labelText;

        const valueWrap = document.createElement("span");
        valueWrap.className = "settingsValue";
        valueWrap.appendChild(controlEl);
        if (hintText) {
          const hint = document.createElement("small");
          hint.className = "settingsHint";
          hint.textContent = hintText;
          valueWrap.appendChild(hint);
        }

        row.appendChild(label);
        row.appendChild(valueWrap);
        return row;
      };

      const fontFamilyInput = document.createElement("input");
      fontFamilyInput.type = "text";
      fontFamilyInput.value = currentUiFontFamily;

      const baseFontInput = createNumberInput(currentUiBaseFontSize, 9, 28);
      const calendarDateFontInput = createNumberInput(currentUiCalendarDateFontSize, 9, 28);
      const taskFontInput = createNumberInput(currentUiTaskFontSize, 9, 28);

      const showTaskPanelInput = document.createElement("input");
      showTaskPanelInput.type = "checkbox";
      showTaskPanelInput.checked = currentUiShowTaskPanel;
      const showTaskPanelWrap = document.createElement("span");
      showTaskPanelWrap.className = "settingsCheckboxWrap";
      const showTaskPanelText = document.createElement("span");
      showTaskPanelText.textContent = "Show right-side task panel";
      showTaskPanelWrap.appendChild(showTaskPanelText);
      showTaskPanelWrap.appendChild(showTaskPanelInput);

      const typography = createSection("Typography");
      typography.appendChild(createRow("Font family", fontFamilyInput, "Example: Segoe UI, Yu Gothic UI"));
      typography.appendChild(createRow("Base text size", baseFontInput, "Applied to the overall UI"));
      typography.appendChild(createRow("Calendar date size", calendarDateFontInput, "Day number text in calendar cells"));
      typography.appendChild(createRow("Task list size", taskFontInput, "Task row text size"));

      const layout = createSection("Layout");
      const taskPanelRow = document.createElement("label");
      taskPanelRow.className = "settingsRow settingsRowSingle";
      taskPanelRow.appendChild(showTaskPanelWrap);
      layout.appendChild(taskPanelRow);

      body.appendChild(typography);
      body.appendChild(layout);

      const actions = document.createElement("div");
      actions.className = "taskModalActions settingsActions";
      const cancelButton = document.createElement("button");
      cancelButton.type = "button";
      cancelButton.textContent = "Cancel";
      const applyButton = document.createElement("button");
      applyButton.type = "button";
      applyButton.className = "primary";
      applyButton.textContent = "Apply";
      actions.appendChild(cancelButton);
      actions.appendChild(applyButton);

      modal.appendChild(header);
      modal.appendChild(body);
      modal.appendChild(actions);
      overlay.appendChild(modal);
      document.body.appendChild(overlay);

      const collectDraft = () => ({
        uiFontFamily: normalizeUiFontFamily(fontFamilyInput.value),
        uiBaseFontSize: normalizeUiFontSize(baseFontInput.value, currentUiBaseFontSize),
        uiCalendarDateFontSize: normalizeUiFontSize(calendarDateFontInput.value, currentUiCalendarDateFontSize),
        uiTaskFontSize: normalizeUiFontSize(taskFontInput.value, currentUiTaskFontSize),
        uiShowTaskPanel: !!showTaskPanelInput.checked
      });

      const applyDraft = (draft) => {
        currentUiFontFamily = normalizeUiFontFamily(draft.uiFontFamily);
        currentUiBaseFontSize = normalizeUiFontSize(draft.uiBaseFontSize, currentUiBaseFontSize);
        currentUiCalendarDateFontSize = normalizeUiFontSize(draft.uiCalendarDateFontSize, currentUiCalendarDateFontSize);
        currentUiTaskFontSize = normalizeUiFontSize(draft.uiTaskFontSize, currentUiTaskFontSize);
        currentUiShowTaskPanel = !!draft.uiShowTaskPanel;
        applyUiStyleConfig();
        applyTaskPanelVisibility();
      };

      const restoreSnapshot = () => {
        applyDraft(snapshot);
      };

      const close = (value) => {
        overlay.remove();
        resolve(value);
      };

      [fontFamilyInput, baseFontInput, calendarDateFontInput, taskFontInput].forEach((el) => {
        el.addEventListener("input", () => applyDraft(collectDraft()));
      });
      showTaskPanelInput.addEventListener("change", () => applyDraft(collectDraft()));

      cancelButton.addEventListener("click", () => {
        restoreSnapshot();
        close(null);
      });
      overlay.addEventListener("click", (e) => {
        if (e.target === overlay) {
          restoreSnapshot();
          close(null);
        }
      });
      overlay.addEventListener("keydown", (e) => {
        if (e.key === "Escape") {
          restoreSnapshot();
          close(null);
        }
      });
      applyButton.addEventListener("click", () => {
        const draft = collectDraft();
        applyDraft(draft);
        close(draft);
      });

      modal.tabIndex = -1;
      modal.focus();
    });
  }


  function parseYearMonthInput(raw) {
    const value = (raw || "").trim();
    if (!value) return null;

    let match = value.match(/^(\d{4})\s*[-\/.]\s*(\d{1,2})$/);
    if (!match) {
      match = value.match(/^(\d{4})(\d{2})$/);
    }
    if (!match) return null;

    const year = Number(match[1]);
    const month = Number(match[2]);
    if (!Number.isInteger(year) || !Number.isInteger(month)) return null;
    if (month < 1 || month > 12) return null;
    return { year, month };
  }

  function finishMonthTitleInlineEdit(commit) {
    if (!isMonthTitleEditing) return;

    const input = monthTitleEditInput;
    const raw = input ? input.value : "";

    isMonthTitleEditing = false;
    monthTitleEditInput = null;
    monthTitle.innerHTML = "";

    if (!commit) {
      renderMonthTitle();
      return;
    }

    const parsed = parseYearMonthInput(raw);
    if (!parsed) {
      renderMonthTitle();
      window.alert("Invalid format. Use YYYY-MM (for example: 2026-03).");
      return;
    }

    cursor = new Date(parsed.year, parsed.month - 1, 1);
    selectedDate = new Date(parsed.year, parsed.month - 1, 1);
    renderMonth();
  }

  function startMonthTitleInlineEdit() {
    if (isMonthTitleEditing) return;

    isMonthTitleEditing = true;

    const input = document.createElement("input");
    input.type = "text";
    input.value = currentYearMonthText();
    input.maxLength = 7;
    input.placeholder = "YYYY-MM";
    input.setAttribute("aria-label", "Year and month");
    input.style.width = "8ch";
    input.style.maxWidth = "100%";
    input.style.boxSizing = "border-box";
    input.style.font = "inherit";
    input.style.textAlign = "center";

    monthTitleEditInput = input;
    monthTitle.innerHTML = "";
    monthTitle.appendChild(input);

    let finalized = false;
    const finalize = (commit) => {
      if (finalized) return;
      finalized = true;
      finishMonthTitleInlineEdit(commit);
    };

    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        finalize(true);
      } else if (e.key === "Escape") {
        e.preventDefault();
        finalize(false);
      }
    });

    input.addEventListener("blur", () => finalize(true));

    input.focus();
    input.select();
  }

  async function loadViewConfig() {
    if (!hasHostBridge) {
      return;
    }
    try {
      const response = await hostCall("system.getViewConfig", {});
      const data = response?.data || {};

      const mode = data.defaultViewMode === "timeline" ? "timeline" : "list";
      setViewMode(mode);

      const preset = String(data.defaultRangePresetDays || 1);
      if (["1", "7", "14", "30"].includes(preset)) {
        setRangePreset(preset);
      }

      setCustomRangeDays(Number(data.defaultCustomRangeDays || 7));
      if (data.defaultUseCustomRange) {
        setRangePreset("custom");
      }

      currentUiFontFamily = normalizeUiFontFamily(data.uiFontFamily);
      currentUiBaseFontSize = normalizeUiFontSize(data.uiBaseFontSize, 14);
      currentUiCalendarDateFontSize = normalizeUiFontSize(data.uiCalendarDateFontSize, 13);
      currentUiTaskFontSize = normalizeUiFontSize(data.uiTaskFontSize, 14);
      applyUiStyleConfig();
      currentUiShowTaskPanel = !!data.uiShowTaskPanel;
      applyTaskPanelVisibility();

      const iniPanelRightWidth = Number(data.uiPanelRightWidth || 0);
      if (currentUiShowTaskPanel && Number.isFinite(iniPanelRightWidth) && iniPanelRightWidth > 0) {
        applyPanelRightWidth(iniPanelRightWidth, false);
      }

      const iniCalendarHeight = Number(data.uiCalendarHeight || 0);
      if (Number.isFinite(iniCalendarHeight) && iniCalendarHeight > 0) {
        applyCalendarHeight(iniCalendarHeight, false);
      }
    } catch (_) {
    }
  }

  async function saveViewConfigToIni() {
    if (!hasHostBridge) return;
    try {
      await hostCall("system.setViewConfig", {
        defaultViewMode: currentViewMode,
        rangePreset: currentRangePreset,
        customRangeDays: String(currentCustomRangeDays),
        uiFontFamily: currentUiFontFamily,
        uiBaseFontSize: String(currentUiBaseFontSize),
        uiCalendarDateFontSize: String(currentUiCalendarDateFontSize),
        uiTaskFontSize: String(currentUiTaskFontSize),
        uiPanelRightWidth: String(panelRightWidthForConfig()),
        uiCalendarHeight: String(clampCalendarHeight(readCurrentCalendarHeight())),
        uiShowTaskPanel: currentUiShowTaskPanel ? "1" : "0"
      });
    } catch (_) {
    }
  }

  async function renderMonth() {
    renderMonthTitle();
    renderMonthGrid();
    await loadTasksForCurrentRange();
  }

  taskForm.addEventListener("submit", async (e) => {
    e.preventDefault();

    const title = taskTitle.value.trim();
    const detail = "";
    const startTime = "";
    const endTime = "";
    if (!title) return;

    const timeCheck = validateTimeRange(startTime, endTime);
    if (!timeCheck.ok) {
      window.alert(timeCheck.message);
      return;
    }

    const dateKey = keyOf(selectedDate);

    await withTaskMutation(async () => {
      if (hasHostBridge) {
        await hostCall("task.create", {
          date: dateKey,
          title,
          detail,
          startTime,
          endTime
        });
      } else {
        const tasks = inMemoryTasks.get(dateKey) || [];
        tasks.push({
          id: crypto.randomUUID?.() || String(Date.now()),
          date: dateKey,
          title,
          detail,
          startTime,
          endTime,
          done: false
        });
        inMemoryTasks.set(dateKey, tasks);
      }

      taskTitle.value = "";
      await loadTasksForCurrentRange();
    });
  });

  viewModeSelect.addEventListener("change", () => {
    setViewMode(viewModeSelect.value);
    const range = buildCurrentRange();
    renderCurrentView(range);
    void saveViewConfigToIni();
  });

  timelineLayoutSelect.addEventListener("change", () => {
    setTimelineLayout(timelineLayoutSelect.value, true);
    const range = buildCurrentRange();
    renderCurrentView(range);
  });

  timelineHourStartInput.addEventListener("change", () => {
    setTimelineHourStart(timelineHourStartInput.value, true);
    const range = buildCurrentRange();
    renderCurrentView(range);
  });

  rangePresetSelect.addEventListener("change", async () => {
    setRangePreset(rangePresetSelect.value);
    await loadTasksForCurrentRange();
    void saveViewConfigToIni();
  });

  customRangeDaysInput.addEventListener("change", async () => {
    setCustomRangeDays(customRangeDaysInput.value);
    if (currentRangePreset === "custom") {
      await loadTasksForCurrentRange();
    }
    void saveViewConfigToIni();
  });

  monthTitle.addEventListener("click", (e) => {
    if (isMonthTitleEditing) return;
    const target = e.target;
    if (!(target instanceof HTMLElement)) return;
    if (!target.closest(".monthTitleText")) return;
    startMonthTitleInlineEdit();
  });
  monthTitle.title = "Click to edit year-month";

  document.getElementById("prevMonth").addEventListener("click", () => {
    cursor = new Date(cursor.getFullYear(), cursor.getMonth() - 1, 1);
    renderMonth();
  });

  document.getElementById("nextMonth").addEventListener("click", () => {
    cursor = new Date(cursor.getFullYear(), cursor.getMonth() + 1, 1);
    renderMonth();
  });

  settingsButton.addEventListener("click", async () => {
    const next = await openSettingsDialog();
    if (!next) return;
    void saveViewConfigToIni();
  });

  applyUiStyleConfig();
  applyTaskPanelVisibility();
  setViewMode("list");
  setRangePreset("1");
  setCustomRangeDays(7);
  setTimelineLayout(readSavedTimelineLayout(), false);
  setTimelineHourStart(readSavedTimelineHourStart(), false);
  initializePanelSplitter();
  initializeCalendarHeightSplitter();
  syncLayoutSplitterHeight();

  (async () => {
    await loadViewConfig();
    await renderMonth();
  })();
})();
