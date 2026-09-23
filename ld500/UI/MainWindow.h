#pragma once

// Windows-specific main application window: message handling, mouse/zoom-slider input, and menu
// commands. Owns the top-level RadarGridModel/ObjectTracker instances the acquisition threads and
// renderer operate on.

#include <windows.h>
#include <atomic>
#include "RadarGridModel.h"
#include "ObjectTracking.h"

extern RadarGridModel     g_GridModel;
extern ObjectTracker      g_Tracker;
// Object tracking (centroid clustering + persistent IDs) is off by default; toggled via the
// Tracking menu. When disabled, no clustering work is done and no red track markers are drawn.
extern std::atomic<bool>  g_TrackingEnabled;

// Registers the "RadarWindow" window class. Must be called once before CreateMainWindow.
ATOM RegisterMainWindowClass(HINSTANCE hInstance);

// Creates and shows the main application window.
HWND CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
