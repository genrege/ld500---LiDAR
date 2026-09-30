#include "ImGuiShell.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif


#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Platform/ISerialPort.h"
#include "Platform/ISettingsStore.h"
#include "RadarGridModel.h"
#include "ObjectTracking.h"
#include "RadarProtocol.h"
#include "RadarRendererImGui.h"

namespace {
    constexpr uint32_t kDefaultBaudRate = 230400;

    // Independent from the Win32 shell's g_GridModel/g_Tracker/g_TrackingEnabled/etc (in
    // UI/MainWindow.cpp) - the two shells never run in the same process at once, so there's no
    // need to share state, and keeping them separate avoids coupling this shell to Win32-only code.
    RadarGridModel    s_GridModel;
    ObjectTracker     s_Tracker;
    std::atomic<bool> s_TrackingEnabled(false);
    std::atomic<bool> s_ShadowCastEnabled(false);
    std::atomic<bool> s_KeepRunning(true);

    std::mutex                    s_SerialSettingsMutex;
    std::string                   s_ComPortName;
    uint32_t                      s_BaudRate = kDefaultBaudRate;
    std::unique_ptr<ISerialPort>  s_SerialPort;
    std::atomic<bool>             s_SerialConnected(false);

    std::unique_ptr<ISettingsStore> s_Settings;

    bool s_ZoomSliderDragging = false;
    bool s_IntensitySliderDragging = false;
    bool s_ShowSettingsDialog = false;
    bool s_ShowPortsDialog = false;
    bool s_ShowAboutDialog = false;

    std::vector<SerialPortInfo> s_CachedPorts;
    int s_SelectedPortIndex = -1;
    double s_LastPortRefreshTime = -1000.0;

    void GlfwErrorCallback(int error, const char* description) {
        std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
    }

    bool ConnectToPort(const std::string& portName, uint32_t baudRate) {
        std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
        s_ComPortName = portName;
        s_BaudRate = baudRate;
        if (!s_SerialPort) s_SerialPort = CreatePlatformSerialPort();
        bool ok = !portName.empty() && s_SerialPort->Open(portName, baudRate);
        s_SerialConnected.store(ok, std::memory_order_release);
        return ok;
    }

    bool ResetActivePort() {
        std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
        if (!s_SerialPort) return false;
        bool ok = s_SerialPort->Reset();
        s_SerialConnected.store(ok, std::memory_order_release);
        return ok;
    }

    // Background thread: continuously reads bytes from the active port, parses them into radar
    // readings, and ingests them into s_GridModel. Mirrors SerialReadThread in
    // Acquisition/SerialPort.cpp, but driven through ISerialPort instead of the legacy globals.
    void SerialReadThreadFunc() {
        std::vector<uint8_t> streamAccumulator;
        constexpr int BUF_SZ = 2048;
        uint8_t rxBuffer[BUF_SZ];

        while (s_KeepRunning.load(std::memory_order_acquire)) {
            bool isOpen;
            {
                std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
                isOpen = s_SerialPort && s_SerialPort->IsOpen();
            }
            if (!isOpen) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            int bytesRead;
            {
                std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
                bytesRead = s_SerialPort->Read(rxBuffer, BUF_SZ);
            }

            if (bytesRead > 0) {
                streamAccumulator.insert(streamAccumulator.end(), rxBuffer, rxBuffer + bytesRead);

                std::vector<RadarReading> parsedReadings;
                size_t consumed = 0;
                while (ParseRadarStream(streamAccumulator, parsedReadings, consumed)) {
                    if (consumed == LD_PACKET_SIZE && !parsedReadings.empty()) {
                        s_GridModel.IngestReadings(parsedReadings);
                    }
                    streamAccumulator.erase(streamAccumulator.begin(), streamAccumulator.begin() + consumed);
                    consumed = 0;
                }
            } else if (bytesRead < 0) {
                std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
                if (s_SerialPort) s_SerialPort->Close();
                s_SerialConnected.store(false, std::memory_order_release);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void DrawMenuBar(GLFWwindow* window) {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Settings...")) s_ShowSettingsDialog = true;
            if (ImGui::MenuItem("Manage Ports...")) s_ShowPortsDialog = true;
            ImGui::Separator();
#if defined(_WIN32)
            if (ImGui::MenuItem("Switch to Win32 UI (restart required)")) {
                s_Settings->SetString("UiBackend", "win32");
                ImGui::OpenPopup("RestartRequired");
            }
#endif
            if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            bool tracking = s_TrackingEnabled.load(std::memory_order_relaxed);
            if (ImGui::MenuItem("Tracking", nullptr, tracking)) {
                tracking = !tracking;
                s_TrackingEnabled.store(tracking, std::memory_order_relaxed);
                s_Settings->SetBool("TrackingEnabled", tracking);
            }
            bool shadow = s_ShadowCastEnabled.load(std::memory_order_relaxed);
            if (ImGui::MenuItem("Shadow Cast", nullptr, shadow)) {
                shadow = !shadow;
                s_ShadowCastEnabled.store(shadow, std::memory_order_relaxed);
                s_Settings->SetBool("ShadowCastEnabled", shadow);
            }
            bool persistence = s_GridModel.GetPersistenceEnabled();
            if (ImGui::MenuItem("Persistence", nullptr, persistence)) {
                persistence = !persistence;
                s_GridModel.SetPersistenceEnabled(persistence);
                s_Settings->SetBool("PersistenceEnabled", persistence);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About...")) s_ShowAboutDialog = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginPopupModal("RestartRequired", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Restart LD500 for the Win32 UI to take effect.");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::EndMainMenuBar();
    }

    // Mirrors PortManagerDlgProc: periodically refreshes the detected-port list, plus
    // Refresh/Reset buttons. Additionally lets the user pick+connect to a port from the list,
    // since (unlike Windows' hardcoded "COM3" default) there's no fixed default device path on
    // macOS/Linux to fall back to.
    void DrawPortsDialog() {
        if (!s_ShowPortsDialog) return;
        ImGui::Begin("Manage Ports", &s_ShowPortsDialog);

        double now = ImGui::GetTime();
        if (now - s_LastPortRefreshTime > 2.0) {
            s_CachedPorts = EnumerateSerialPorts();
            s_LastPortRefreshTime = now;
        }

        std::string activePortName;
        bool connected = s_SerialConnected.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
            activePortName = s_ComPortName;
        }

        if (s_CachedPorts.empty()) {
            ImGui::TextUnformatted("No serial ports detected");
        } else {
            for (int i = 0; i < static_cast<int>(s_CachedPorts.size()); ++i) {
                const SerialPortInfo& port = s_CachedPorts[i];
                bool isActive = (port.portName == activePortName);
                std::string label = port.portName + " - " + port.deviceDesc;
                if (isActive) label += connected ? " [ACTIVE]" : " [ACTIVE, DISCONNECTED]";
                if (ImGui::Selectable(label.c_str(), s_SelectedPortIndex == i)) {
                    s_SelectedPortIndex = i;
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Refresh")) {
            s_CachedPorts = EnumerateSerialPorts();
            s_LastPortRefreshTime = now;
        }
        ImGui::SameLine();
        bool canConnect = s_SelectedPortIndex >= 0 && s_SelectedPortIndex < static_cast<int>(s_CachedPorts.size());
        ImGui::BeginDisabled(!canConnect);
        if (ImGui::Button("Connect to Selected Port")) {
            ConnectToPort(s_CachedPorts[s_SelectedPortIndex].portName, s_BaudRate);
            s_Settings->SetString("ComPortName", s_ComPortName);
            s_Settings->SetInt("BaudRate", static_cast<int>(s_BaudRate));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reset Active Port")) {
            bool ok = ResetActivePort();
            ImGui::OpenPopup(ok ? "ResetOk" : "ResetFailed");
        }

        if (ImGui::BeginPopupModal("ResetOk", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Port reset and reconnected successfully.");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("ResetFailed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to reopen the port after reset.");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    // Mirrors SettingsDlgProc: edits the tuning parameters that were previously only reachable
    // via the Win32 Settings dialog, applying and persisting them immediately.
    void DrawSettingsDialog() {
        if (!s_ShowSettingsDialog) return;
        ImGui::Begin("Settings", &s_ShowSettingsDialog);

        static bool initialized = false;
        static int angleOffset, gridSizeCells;
        if (!initialized) {
            angleOffset = static_cast<int>(s_GridModel.GetAngleOffsetDegrees());
            gridSizeCells = GRID_SIZE;
            initialized = true;
        }

        ImGui::SliderInt("Angle Offset (deg)", &angleOffset, 0, 359);
        ImGui::InputInt("Grid Size (cells)", &gridSizeCells);
        ImGui::InputInt("Min Cluster Cells", &MIN_CLUSTER_CELLS);
        ImGui::InputDouble("Max Match Distance (cells)", &MAX_MATCH_DIST_CELLS);
        ImGui::InputInt("Max Missed Frames", &MAX_MISSED_FRAMES);
        ImGui::InputInt("Min Confirm Frames", &MIN_CONFIRM_FRAMES);
        ImGui::InputDouble("Max Static Persistence For Tracking", &MAX_STATIC_PERSISTENCE_FOR_TRACKING);

        if (ImGui::Button("OK")) {
            s_GridModel.SetAngleOffsetDegrees(angleOffset % 360);
            s_Settings->SetDouble("AngleOffsetDegrees", angleOffset % 360);

            if (gridSizeCells > 0 && gridSizeCells != GRID_SIZE) {
                s_GridModel.SetGridSizeCells(gridSizeCells);
                RadarRendererImGui::ResizeGridSurface();
                s_Settings->SetInt("GridSizeCells", gridSizeCells);
            }
            s_Settings->SetInt("MinClusterCells", MIN_CLUSTER_CELLS);
            s_Settings->SetDouble("MaxMatchDistCells", MAX_MATCH_DIST_CELLS);
            s_Settings->SetInt("MaxMissedFrames", MAX_MISSED_FRAMES);
            s_Settings->SetInt("MinConfirmFrames", MIN_CONFIRM_FRAMES);
            s_Settings->SetDouble("MaxStaticPersistenceForTracking", MAX_STATIC_PERSISTENCE_FOR_TRACKING);

            s_ShowSettingsDialog = false;
            initialized = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            s_ShowSettingsDialog = false;
            initialized = false;
        }

        ImGui::End();
    }

    void DrawAboutDialog() {
        if (!s_ShowAboutDialog) return;
        ImGui::Begin("About LD500", &s_ShowAboutDialog, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("LD500 LIDAR Visualizer");
        ImGui::Text("Dear ImGui UI shell");
        if (ImGui::Button("OK")) s_ShowAboutDialog = false;
        ImGui::End();
    }

    // Mirrors the zoom-slider mouse handling from UI/MainWindow.cpp's WndProc
    // (WM_LBUTTONDOWN/WM_MOUSEMOVE/WM_LBUTTONUP/WM_MOUSEWHEEL), driven from ImGui's per-frame
    // mouse state instead of window messages.
    void HandleZoomSliderInput() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) return;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            RadarRendererImGui::HitTestZoomSlider(io.MousePos.x, io.MousePos.y)) {
            s_ZoomSliderDragging = true;
        }
        if (s_ZoomSliderDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            s_GridModel.SetZoomMeters(RadarRendererImGui::ZoomFromSliderY(io.MousePos.y));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s_ZoomSliderDragging) {
            s_ZoomSliderDragging = false;
            s_Settings->SetDouble("ZoomMeters", s_GridModel.GetZoomMeters());
        }
        if (io.MouseWheel != 0.0f) {
            constexpr double ZOOM_STEP_METERS = 0.25;
            double newZoom = s_GridModel.GetZoomMeters() - io.MouseWheel * ZOOM_STEP_METERS;
            if (newZoom < ZOOM_MIN_METERS) newZoom = ZOOM_MIN_METERS;
            if (newZoom > ZOOM_MAX_METERS) newZoom = ZOOM_MAX_METERS;
            s_GridModel.SetZoomMeters(newZoom);
            s_Settings->SetDouble("ZoomMeters", newZoom);
        }
    }

    // Mirrors the background-intensity slider mouse handling added alongside the zoom slider.
    void HandleIntensitySliderInput() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) return;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            RadarRendererImGui::HitTestIntensitySlider(io.MousePos.x, io.MousePos.y)) {
            s_IntensitySliderDragging = true;
        }
        if (s_IntensitySliderDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            RadarRendererImGui::SetBackgroundIntensity(RadarRendererImGui::IntensityFromSliderX(io.MousePos.x));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s_IntensitySliderDragging) {
            s_IntensitySliderDragging = false;
            s_Settings->SetDouble("BackgroundIntensity", RadarRendererImGui::GetBackgroundIntensity());
        }
    }
}

int RunImGuiShell() {
    s_Settings = CreatePlatformSettingsStore();

    s_GridModel.SetGridSizeCells(s_Settings->GetInt("GridSizeCells", 1000));
    MIN_CLUSTER_CELLS = s_Settings->GetInt("MinClusterCells", 20);
    MAX_MATCH_DIST_CELLS = s_Settings->GetDouble("MaxMatchDistCells", 60.0);
    MAX_MISSED_FRAMES = s_Settings->GetInt("MaxMissedFrames", 20);
    MIN_CONFIRM_FRAMES = s_Settings->GetInt("MinConfirmFrames", 8);
    MAX_STATIC_PERSISTENCE_FOR_TRACKING = s_Settings->GetDouble("MaxStaticPersistenceForTracking", 8.0);
    s_GridModel.SetAngleOffsetDegrees(s_Settings->GetDouble("AngleOffsetDegrees", 0.0));
    s_GridModel.SetPersistenceEnabled(s_Settings->GetBool("PersistenceEnabled", true));
    s_GridModel.SetZoomMeters(s_Settings->GetDouble("ZoomMeters", ZOOM_DEFAULT_METERS));
    RadarRendererImGui::SetBackgroundIntensity(s_Settings->GetDouble("BackgroundIntensity", 1.0));
    s_TrackingEnabled.store(s_Settings->GetBool("TrackingEnabled", false), std::memory_order_relaxed);
    s_ShadowCastEnabled.store(s_Settings->GetBool("ShadowCastEnabled", false), std::memory_order_relaxed);

    std::string savedPort = s_Settings->GetString("ComPortName", "");
    uint32_t savedBaud = static_cast<uint32_t>(s_Settings->GetInt("BaudRate", static_cast<int>(kDefaultBaudRate)));
    if (!savedPort.empty()) ConnectToPort(savedPort, savedBaud);

    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1000, 800, "LD500 Lidar Visualizer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync, replaces the Win32 shell's manual 60 FPS Sleep(16) pump

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    RadarRendererImGui::Init();

    std::thread serialThread(SerialReadThreadFunc);

    double lastFrameTime = glfwGetTime();
    double decayAccumulatorMs = 0.0;
    double freshAccumulatorMs = 0.0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        double dtMs = (now - lastFrameTime) * 1000.0;
        lastFrameTime = now;

        // Ticks the same decay/fresh-marker upkeep the Win32 shell runs on separate threads
        // (AppWorkers.cpp), but folded into this shell's own frame loop instead of spawning more
        // threads, since the loop already runs at a steady vsynced cadence.
        decayAccumulatorMs += dtMs;
        while (decayAccumulatorMs >= CELL_DECAY_INTERVAL_MS) {
            s_GridModel.TickDecay();
            decayAccumulatorMs -= CELL_DECAY_INTERVAL_MS;
        }
        freshAccumulatorMs += dtMs;
        while (freshAccumulatorMs >= 16.0) {
            s_GridModel.TickFreshMarkers();
            freshAccumulatorMs -= 16.0;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RadarRendererImGui::LayoutZoomSlider();
        RadarRendererImGui::LayoutIntensitySlider();
        HandleZoomSliderInput();
        HandleIntensitySliderInput();

        std::string hudPortName;
        uint32_t hudBaudRate;
        {
            std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
            hudPortName = s_ComPortName.empty() ? "(none)" : s_ComPortName;
            hudBaudRate = s_BaudRate;
        }
        RadarRendererImGui::PaintRadar(s_GridModel, s_Tracker,
            s_TrackingEnabled.load(std::memory_order_relaxed),
            s_ShadowCastEnabled.load(std::memory_order_relaxed),
            hudPortName.c_str(), hudBaudRate, s_SerialConnected.load(std::memory_order_acquire));

        DrawMenuBar(window);
        DrawPortsDialog();
        DrawSettingsDialog();
        DrawAboutDialog();

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    s_KeepRunning.store(false, std::memory_order_release);
    serialThread.join();
    {
        std::lock_guard<std::mutex> lock(s_SerialSettingsMutex);
        if (s_SerialPort) s_SerialPort->Close();
    }

    RadarRendererImGui::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
