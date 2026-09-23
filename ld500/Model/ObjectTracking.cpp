#include "ObjectTracking.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <functional>

void ObjectTracker::Update(const RadarGridSnapshot& snapshot, bool trackingEnabled) {
    if (!m_Initialized) {
        m_LastSeenResetGeneration = snapshot.resetGeneration;
        m_Initialized = true;
    }
    if (snapshot.resetGeneration != m_LastSeenResetGeneration) {
        m_Tracks.clear();
        m_LastSeenResetGeneration = snapshot.resetGeneration;
    }

    if (!trackingEnabled) {
        // Tracking is disabled (default): drop any existing tracks and skip the clustering pass entirely.
        m_Tracks.clear();
        return;
    }

    // --- Connected-component clustering over active cells (union-find, single pass) ---
    std::vector<int> cellLabel(static_cast<size_t>(GRID_SIZE) * GRID_SIZE, -1);
    std::vector<int> ufParent;
    std::function<int(int)> findRoot = [&](int a) {
        while (ufParent[a] != a) {
            ufParent[a] = ufParent[ufParent[a]];
            a = ufParent[a];
        }
        return a;
        };
    auto uniteRoots = [&](int a, int b) {
        int ra = findRoot(a), rb = findRoot(b);
        if (ra != rb) ufParent[ra] = rb;
        };

    for (int cy = 0; cy < GRID_SIZE; ++cy) {
        for (int cx = 0; cx < GRID_SIZE; ++cx) {
            size_t idx = static_cast<size_t>(cy) * GRID_SIZE + cx;
            if (snapshot.intensity[idx] == 0) continue;

            // Only look at already-scanned neighbours (upper-left, up, upper-right, left)
            static const int ndx[4] = { -1, 0, 1, -1 };
            static const int ndy[4] = { -1, -1, -1, 0 };
            int neighbourLabel = -1;
            for (int k = 0; k < 4; ++k) {
                int nx = cx + ndx[k], ny = cy + ndy[k];
                if (nx < 0 || nx >= GRID_SIZE || ny < 0) continue;
                size_t nIdx = static_cast<size_t>(ny) * GRID_SIZE + nx;
                if (cellLabel[nIdx] == -1) continue;
                if (neighbourLabel == -1) {
                    neighbourLabel = cellLabel[nIdx];
                }
                else {
                    uniteRoots(neighbourLabel, cellLabel[nIdx]);
                }
            }

            if (neighbourLabel == -1) {
                int newLabel = static_cast<int>(ufParent.size());
                ufParent.push_back(newLabel);
                cellLabel[idx] = newLabel;
            }
            else {
                cellLabel[idx] = neighbourLabel;
            }
        }
    }

    struct ClusterAccum { double sumX = 0, sumY = 0, sumPersistence = 0; int count = 0; };
    std::unordered_map<int, ClusterAccum> clusterAccums;
    for (int cy = 0; cy < GRID_SIZE; ++cy) {
        for (int cx = 0; cx < GRID_SIZE; ++cx) {
            size_t idx = static_cast<size_t>(cy) * GRID_SIZE + cx;
            if (cellLabel[idx] == -1) continue;
            int root = findRoot(cellLabel[idx]);
            ClusterAccum& acc = clusterAccums[root];
            acc.sumX += cx;
            acc.sumY += cy;
            acc.sumPersistence += snapshot.persistence[idx];
            acc.count++;
        }
    }

    std::vector<std::pair<double, double>> clusterCentroids;
    for (const auto& kv : clusterAccums) {
        if (kv.second.count < MIN_CLUSTER_CELLS) continue;
        double avgPersistence = kv.second.sumPersistence / kv.second.count;
        if (avgPersistence > MAX_STATIC_PERSISTENCE_FOR_TRACKING) continue; // Skip static background clutter
        clusterCentroids.emplace_back(kv.second.sumX / kv.second.count, kv.second.sumY / kv.second.count);
    }

    // --- Greedy nearest-centroid tracking: associate clusters with persistent track IDs ---
    std::vector<bool> clusterMatched(clusterCentroids.size(), false);
    std::vector<bool> trackMatched(m_Tracks.size(), false);

    struct MatchCandidate { double dist; size_t trackIdx; size_t clusterIdx; };
    std::vector<MatchCandidate> matchCandidates;
    for (size_t t = 0; t < m_Tracks.size(); ++t) {
        for (size_t c = 0; c < clusterCentroids.size(); ++c) {
            double dx = m_Tracks[t].gx - clusterCentroids[c].first;
            double dy = m_Tracks[t].gy - clusterCentroids[c].second;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= MAX_MATCH_DIST_CELLS) {
                matchCandidates.push_back({ dist, t, c });
            }
        }
    }
    std::sort(matchCandidates.begin(), matchCandidates.end(),
        [](const MatchCandidate& a, const MatchCandidate& b) { return a.dist < b.dist; });

    for (const auto& cand : matchCandidates) {
        if (trackMatched[cand.trackIdx] || clusterMatched[cand.clusterIdx]) continue;
        trackMatched[cand.trackIdx] = true;
        clusterMatched[cand.clusterIdx] = true;
        m_Tracks[cand.trackIdx].gx = clusterCentroids[cand.clusterIdx].first;
        m_Tracks[cand.trackIdx].gy = clusterCentroids[cand.clusterIdx].second;
        m_Tracks[cand.trackIdx].missedFrames = 0;
        if (m_Tracks[cand.trackIdx].hitStreak < MIN_CONFIRM_FRAMES) {
            m_Tracks[cand.trackIdx].hitStreak++;
        }
    }

    for (size_t t = 0; t < m_Tracks.size(); ++t) {
        if (!trackMatched[t]) {
            m_Tracks[t].missedFrames++;
            // Unconfirmed tracks (likely noise) are dropped as soon as they miss a frame,
            // rather than lingering for MAX_MISSED_FRAMES like confirmed tracks.
            if (m_Tracks[t].hitStreak < MIN_CONFIRM_FRAMES) {
                m_Tracks[t].missedFrames = MAX_MISSED_FRAMES + 1;
            }
        }
    }
    m_Tracks.erase(std::remove_if(m_Tracks.begin(), m_Tracks.end(),
        [](const TrackedObject& obj) { return obj.missedFrames > MAX_MISSED_FRAMES; }),
        m_Tracks.end());

    for (size_t c = 0; c < clusterCentroids.size(); ++c) {
        if (clusterMatched[c]) continue;
        TrackedObject obj;
        obj.id = m_NextTrackId++;
        obj.gx = clusterCentroids[c].first;
        obj.gy = clusterCentroids[c].second;
        obj.missedFrames = 0;
        obj.hitStreak = 1;
        m_Tracks.push_back(obj);
    }
}
