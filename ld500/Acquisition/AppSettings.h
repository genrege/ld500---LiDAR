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
