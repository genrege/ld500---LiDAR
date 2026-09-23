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

constexpr int    MIN_CLUSTER_CELLS = 20;          // Ignore small noise blips (larger = less sensitive)
constexpr double MAX_MATCH_DIST_CELLS = 60.0;     // Max grid-cell distance to associate a cluster with a track
constexpr int    MAX_MISSED_FRAMES = 20;          // Frames a track may go unmatched before being dropped
constexpr int    MIN_CONFIRM_FRAMES = 8;          // Consecutive matched frames required before a track is shown
constexpr double MAX_STATIC_PERSISTENCE_FOR_TRACKING = 8.0; // Clusters averaging above this persistence are static background, not tracked

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
