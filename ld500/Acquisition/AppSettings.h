#pragma once

// Windows-specific persisted app settings (registry-backed).

// Loads the saved LIDAR orientation offset (degrees, [0, 360)), defaulting to 0 if unset/invalid.
double LoadAngleOffsetDegrees();

// Persists the LIDAR orientation offset to the registry immediately.
void SaveAngleOffsetDegrees(double offsetDegrees);

// Loads the saved object-tracking toggle, defaulting to off (false) if unset.
bool LoadTrackingEnabled();

// Persists the object-tracking toggle to the registry immediately.
void SaveTrackingEnabled(bool enabled);

// Loads the saved radar shadow-cast toggle, defaulting to off (false) if unset.
bool LoadShadowCastEnabled();

// Persists the radar shadow-cast toggle to the registry immediately.
void SaveShadowCastEnabled(bool enabled);

// Loads the saved persistence-highlighting toggle, defaulting to on (true) if unset.
bool LoadPersistenceEnabled();

// Persists the persistence-highlighting toggle to the registry immediately.
void SavePersistenceEnabled(bool enabled);

// Loads the saved grid size (cells per side), defaulting to 1000 if unset/invalid. Takes effect
// on next launch only (the grid's off-screen DIB surface is sized once at startup).
int LoadGridSizeCells();

// Persists the grid size to the registry immediately.
void SaveGridSizeCells(int cells);

// Loads the saved object-tracking tuning parameters, defaulting to the values below if unset.
int LoadMinClusterCells();             // Default: 20
double LoadMaxMatchDistCells();        // Default: 60.0
int LoadMaxMissedFrames();             // Default: 20
int LoadMinConfirmFrames();            // Default: 8
double LoadMaxStaticPersistenceForTracking(); // Default: 8.0

// Persists the object-tracking tuning parameters to the registry immediately.
void SaveMinClusterCells(int cells);
void SaveMaxMatchDistCells(double cells);
void SaveMaxMissedFrames(int frames);
void SaveMinConfirmFrames(int frames);
void SaveMaxStaticPersistenceForTracking(double persistence);
