#include "AppWorkers.h"
#include "SerialPort.h"
#include "RadarGridModel.h"

DWORD WINAPI DecayThread(LPVOID lpParam) {
    RadarGridModel* model = static_cast<RadarGridModel*>(lpParam);
    while (g_KeepRunning) {
        Sleep(CELL_DECAY_INTERVAL_MS);
        model->TickDecay();
    }
    return 0;
}

DWORD WINAPI FreshMarkerDecayThread(LPVOID lpParam) {
    RadarGridModel* model = static_cast<RadarGridModel*>(lpParam);
    const int FRESH_TICK_MS = 16; // Roughly one paint frame, decoupled from the fast intensity decay
    while (g_KeepRunning) {
        Sleep(FRESH_TICK_MS);
        model->TickFreshMarkers();
    }
    return 0;
}
