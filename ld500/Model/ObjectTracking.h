#pragma once

// Portable object tracking: connected-component clustering over the grid plus greedy
// nearest-centroid association to persistent track IDs across frames. No Windows dependency.

#include <vector>
#include "RadarGridModel.h"

// A single tracked object's centroid and confirmation state, carried between frames.
struct TrackedObject {
    int id;
    double gx, gy;      // Grid-space centroid position
    int missedFrames;   // Consecutive frames without a matching cluster
    int hitStreak;      // Consecutive frames matched; must reach MIN_CONFIRM_FRAMES to be shown
};

// Object-tracking tuning parameters, persisted settings (defaults shown below).
extern int    MIN_CLUSTER_CELLS;          // Ignore small noise blips (larger = less sensitive); default 20
extern double MAX_MATCH_DIST_CELLS;       // Max grid-cell distance to associate a cluster with a track; default 60.0
extern int    MAX_MISSED_FRAMES;          // Frames a track may go unmatched before being dropped; default 20
extern int    MIN_CONFIRM_FRAMES;         // Consecutive matched frames required before a track is shown; default 8
extern double MAX_STATIC_PERSISTENCE_FOR_TRACKING; // Clusters averaging above this persistence are static background, not tracked; default 8.0

// Persistent clustering/tracking state carried between paint frames.
class ObjectTracker {
public:
    // Recomputes clusters from the given grid snapshot and updates persistent track IDs in place.
    // When trackingEnabled is false, all tracks are dropped and no clustering work is done.
    void Update(const RadarGridSnapshot& snapshot, bool trackingEnabled);

    const std::vector<TrackedObject>& Tracks() const { return m_Tracks; }

private:
    std::vector<TrackedObject> m_Tracks;
    int  m_NextTrackId = 1;
    int  m_LastSeenResetGeneration = 0;
    bool m_Initialized = false;
};
