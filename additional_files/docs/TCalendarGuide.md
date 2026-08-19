# TCalendar User Guide

Last updated: 2026-03-08  
TCalendar is a desktop calendar and task app for Windows that keeps your schedule and task data locally.

## What You Can Do With TCalendar

Use TCalendar when you want a lightweight local calendar window instead of a browser-based or cloud-only calendar.

Typical uses:
- view your schedule in a calendar window
- keep task lists by date
- manage tasks in a local app without a separate web service
- keep calendar data in the same folder as the app

## Main Files

- `TCalendar.exe`: the application
- `data/tasks.db`: local task database

## Start TCalendar

```bat
TCalendar.exe
```

## How It Works

TCalendar opens as its own window and stores data locally in the application folder.
That means:
- your task data stays with the app folder
- some window and UI preferences may be remembered
- moving the app folder also moves your local data if you keep the `data/` folder with it

## What You Can Expect

- a calendar window focused on local task management
- local storage instead of a required online account
- a self-contained app layout that keeps its files together

## Tips

- Keep the application files together in one folder.
- If you move the app, move its `data/` folder with it.
- If you use a custom template or customized files, keep those with the same application folder.
- Keep the application folder writable so settings and window state can be saved.

## Related Docs

- Core implementation docs live under `source/tcal/docs/`
