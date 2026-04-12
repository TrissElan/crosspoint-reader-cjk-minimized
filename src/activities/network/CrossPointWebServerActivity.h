#pragma once

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"

// Web server activity states
enum class WebServerActivityState {
  AP_STARTING,    // Starting Access Point mode
  SERVER_RUNNING, // Web server is running and handling requests
  SHUTTING_DOWN   // Shutting down server and WiFi
};

/**
 * CrossPointWebServerActivity is the entry point for file transfer functionality.
 * It creates a WiFi Access Point (hotspot) that clients can connect to,
 * then starts the CrossPointWebServer for browser-based file management.
 */
class CrossPointWebServerActivity final : public Activity {
  WebServerActivityState state = WebServerActivityState::AP_STARTING;

  // Web server - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  void renderServerRunning() const;

  void startAccessPoint();
  void startWebServer();
  void stopWebServer();

 public:
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CrossPointWebServer", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
