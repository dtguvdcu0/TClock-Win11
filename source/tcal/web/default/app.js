(() => {
  "use strict";

  const monthTitle = document.getElementById("monthTitle");
  const monthGrid = document.getElementById("monthGrid");
  const dayTitle = document.getElementById("dayTitle");
  const taskList = document.getElementById("taskList");
  const taskForm = document.getElementById("taskForm");
  const taskTitle = document.getElementById("taskTitle");

  let cursor = new Date();
  let selectedDate = new Date();
  const inMemoryTasks = new Map();

  const hasHostBridge = !!(window.chrome && window.chrome.webview);
  const pendingRequests = new Map();
  let requestSeq = 0;
  let mutationInFlight = false;
  let loadSeq = 0;

  const weekdayLabels = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

  function keyOf(d) {
    return d.toISOString().slice(0, 10);
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

      pendingRequests.set(requestId, {
        resolve,
        reject,
        timeoutId
      });

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

      if (!response || typeof response !== "object" || !response.requestId) {
        return;
      }

      const pending = pendingRequests.get(response.requestId);
      if (!pending) {
        return;
      }

      clearTimeout(pending.timeoutId);
      pendingRequests.delete(response.requestId);

      if (response.ok) {
        pending.resolve(response);
      } else {
        pending.reject(new Error(response.code || "HOST_ERROR"));
      }
    });
  }


  function refreshBusyUiState() {
    taskTitle.disabled = mutationInFlight;
    const submitButton = taskForm.querySelector('button[type="submit"]');
    if (submitButton) {
      submitButton.disabled = mutationInFlight;
    }

    const controls = taskList.querySelectorAll('input, button');
    controls.forEach((el) => {
      el.disabled = mutationInFlight;
    });
  }

  async function withTaskMutation(run) {
    if (mutationInFlight) {
      return false;
    }

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

  function updateLocalTaskTitle(taskId, title) {
    const key = keyOf(selectedDate);
    const tasks = inMemoryTasks.get(key) || [];
    const target = tasks.find((item) => item.id === taskId);
    if (target) {
      target.title = title;
    }
  }

  function updateLocalTaskDone(taskId, done) {
    const key = keyOf(selectedDate);
    const tasks = inMemoryTasks.get(key) || [];
    const target = tasks.find((item) => item.id === taskId);
    if (target) {
      target.done = !!done;
    }
  }

  function renderTaskList(tasks) {
    taskList.innerHTML = "";
    for (const t of tasks) {
      const li = document.createElement("li");
      const row = document.createElement("label");
      row.className = "taskRow";

      const check = document.createElement("input");
      check.type = "checkbox";
      check.checked = !!t.done;

      const title = document.createElement("span");
      title.className = "taskTitle";
      title.textContent = t.title;
      if (t.done) {
        title.classList.add("isDone");
      }

      check.addEventListener("change", async () => {
        const nextDone = check.checked;
        const prevDone = !nextDone;

        const executed = await withTaskMutation(async () => {
          if (hasHostBridge && t.id) {
            await hostCall("task.toggleDone", { id: t.id, done: nextDone });
            await loadTasksForSelectedDay();
            return;
          }

          updateLocalTaskDone(t.id, nextDone);
          if (nextDone) {
            title.classList.add("isDone");
          } else {
            title.classList.remove("isDone");
          }
        });

        if (!executed) {
          check.checked = prevDone;
          return;
        }
      });

      const edit = document.createElement("button");
      edit.type = "button";
      edit.className = "taskEdit";
      edit.textContent = "Edit";
      edit.addEventListener("click", async () => {
        const nextTitleRaw = window.prompt("Edit task title", t.title || "");
        if (nextTitleRaw === null) return;

        const nextTitle = nextTitleRaw.trim();
        if (!nextTitle || nextTitle === t.title) return;

        await withTaskMutation(async () => {
          if (hasHostBridge && t.id) {
            await hostCall("task.updateTitle", { id: t.id, title: nextTitle });
            await loadTasksForSelectedDay();
            return;
          }

          updateLocalTaskTitle(t.id, nextTitle);
          await loadTasksForSelectedDay();
        });
      });

      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "taskDelete";
      remove.textContent = "Delete";
      remove.addEventListener("click", async () => {
        await withTaskMutation(async () => {
          if (hasHostBridge && t.id) {
            await hostCall("task.delete", { id: t.id });
            await loadTasksForSelectedDay();
            return;
          }

          const key = keyOf(selectedDate);
          const items = inMemoryTasks.get(key) || [];
          inMemoryTasks.set(key, items.filter((item) => item.id !== t.id));
          await loadTasksForSelectedDay();
        });
      });

      row.appendChild(check);
      row.appendChild(title);
      row.appendChild(edit);
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

  async function loadTasksForSelectedDay() {
    const key = keyOf(selectedDate);
    const currentSeq = ++loadSeq;

    if (hasHostBridge) {
      try {
        const response = await hostCall("calendar.getDayTasks", { date: key });
        if (currentSeq !== loadSeq) return;
        const items = response?.data?.items;
        renderTaskList(Array.isArray(items) ? items : []);
        return;
      } catch (_) {
      }
    }

    if (currentSeq !== loadSeq) return;
    const tasks = inMemoryTasks.get(key) || [];
    renderTaskList(tasks);
  }

  function createDayCell(date, inCurrentMonth) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "dayCell";
    if (!inCurrentMonth) {
      button.classList.add("isOutside");
    }
    if (sameDay(date, selectedDate)) {
      button.classList.add("isSelected");
    }

    const dateLabel = document.createElement("span");
    dateLabel.className = "dateLabel";
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

    const daysRow = document.createElement("div");
    daysRow.className = "daysGrid";

    const year = cursor.getFullYear();
    const month = cursor.getMonth();
    const firstDay = new Date(year, month, 1);
    const firstWeekday = firstDay.getDay();

    for (let i = 0; i < 42; i += 1) {
      const offset = i - firstWeekday;
      const d = new Date(year, month, 1 + offset);
      const inCurrentMonth = d.getMonth() === month;
      daysRow.appendChild(createDayCell(d, inCurrentMonth));
    }

    monthGrid.appendChild(daysRow);
  }

  function renderMonth() {
    monthTitle.textContent = cursor.toLocaleDateString(undefined, { year: "numeric", month: "long" });
    renderMonthGrid();
    dayTitle.textContent = `Tasks (${selectedDate.toLocaleDateString()})`;
    void loadTasksForSelectedDay();
  }

  document.getElementById("prevMonth").addEventListener("click", () => {
    cursor = new Date(cursor.getFullYear(), cursor.getMonth() - 1, 1);
    renderMonth();
  });

  document.getElementById("nextMonth").addEventListener("click", () => {
    cursor = new Date(cursor.getFullYear(), cursor.getMonth() + 1, 1);
    renderMonth();
  });

  taskForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    const title = taskTitle.value.trim();
    if (!title) return;

    const key = keyOf(selectedDate);

    await withTaskMutation(async () => {
      if (hasHostBridge) {
        await hostCall("task.create", { date: key, title });
        taskTitle.value = "";
        await loadTasksForSelectedDay();
        return;
      }

      const tasks = inMemoryTasks.get(key) || [];
      tasks.push({ id: crypto.randomUUID?.() || String(Date.now()), title, done: false });
      inMemoryTasks.set(key, tasks);
      taskTitle.value = "";
      renderTaskList(tasks);
    });
  });

  renderMonth();
})();
