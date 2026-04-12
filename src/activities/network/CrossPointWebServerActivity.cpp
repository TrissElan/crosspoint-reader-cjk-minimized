#include "CrossPointWebServerActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <cstddef>

#include "state/MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"

namespace {
// AP Mode configuration
constexpr const char* AP_SSID = "CrossPoint-Reader";
constexpr const char* AP_PASSWORD = nullptr;  // Open network for ease of use
constexpr const char* AP_HOSTNAME = "cp";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;
constexpr int QR_CODE_WIDTH = 198;
constexpr int QR_CODE_HEIGHT = 198;

// DNS server for captive portal (redirects all DNS queries to our IP)
DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;
}  // namespace

void CrossPointWebServerActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("WEBACT", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Reset state
  state = WebServerActivityState::AP_STARTING;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  requestUpdate();

  startAccessPoint();
}

void CrossPointWebServerActivity::onExit() {
  Activity::onExit();

  LOG_DBG("WEBACT", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

  state = WebServerActivityState::SHUTTING_DOWN;

  // Stop the web server first (before disconnecting WiFi)
  stopWebServer();

  // Stop mDNS
  MDNS.end();

  // Stop DNS server if running
  if (dnsServer) {
    LOG_DBG("WEBACT", "Stopping DNS server...");
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }

  // Brief wait for LWIP stack to flush pending packets
  delay(50);

  LOG_DBG("WEBACT", "Stopping WiFi AP...");
  WiFi.softAPdisconnect(true);
  delay(30);

  LOG_DBG("WEBACT", "Setting WiFi mode OFF...");
  WiFi.mode(WIFI_OFF);
  delay(30);

  LOG_DBG("WEBACT", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServerActivity::startAccessPoint() {
  LOG_DBG("WEBACT", "Starting Access Point mode...");
  LOG_DBG("WEBACT", "Free heap before AP start: %d bytes", ESP.getFreeHeap());

  // Configure and start the AP
  WiFi.mode(WIFI_AP);
  delay(100);

  // Start soft AP (open network — no password)
  const bool apStarted = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);

  if (!apStarted) {
    LOG_ERR("WEBACT", "ERROR: Failed to start Access Point!");
    activityManager.goHome();
    return;
  }

  delay(100);  // Wait for AP to fully initialize

  // Get AP IP address
  const IPAddress apIP = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  connectedIP = ipStr;
  connectedSSID = AP_SSID;

  LOG_DBG("WEBACT", "Access Point started!");
  LOG_DBG("WEBACT", "SSID: %s", AP_SSID);
  LOG_DBG("WEBACT", "IP: %s", connectedIP.c_str());

  // Start mDNS for hostname resolution
  if (MDNS.begin(AP_HOSTNAME)) {
    LOG_DBG("WEBACT", "mDNS started: http://%s.local/", AP_HOSTNAME);
  } else {
    LOG_DBG("WEBACT", "WARNING: mDNS failed to start");
  }

  // Start DNS server for captive portal behavior
  // This redirects all DNS queries to our IP, making any domain typed resolve to us
  dnsServer = new DNSServer();
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", apIP);
  LOG_DBG("WEBACT", "DNS server started for captive portal");

  LOG_DBG("WEBACT", "Free heap after AP start: %d bytes", ESP.getFreeHeap());

  startWebServer();
}

void CrossPointWebServerActivity::startWebServer() {
  LOG_DBG("WEBACT", "Starting web server...");

  webServer.reset(new CrossPointWebServer());
  webServer->begin();

  if (webServer->isRunning()) {
    state = WebServerActivityState::SERVER_RUNNING;
    LOG_DBG("WEBACT", "Web server started successfully");
    requestUpdate();
  } else {
    LOG_ERR("WEBACT", "ERROR: Failed to start web server!");
    webServer.reset();
    activityManager.goHome();
  }
}

void CrossPointWebServerActivity::stopWebServer() {
  if (webServer && webServer->isRunning()) {
    LOG_DBG("WEBACT", "Stopping web server...");
    webServer->stop();
    LOG_DBG("WEBACT", "Web server stopped");
  }
  webServer.reset();
}

void CrossPointWebServerActivity::loop() {
  if (state == WebServerActivityState::SERVER_RUNNING) {
    // Handle DNS requests for captive portal
    if (dnsServer) {
      dnsServer->processNextRequest();
    }

    // Handle web server requests - maximize throughput with watchdog safety
    if (webServer && webServer->isRunning()) {
      const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;

      // Log if there's a significant gap between handleClient calls (>100ms)
      if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
        LOG_DBG("WEBACT", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
      }

      // Reset watchdog BEFORE processing - HTTP header parsing can be slow
      esp_task_wdt_reset();

      // Process HTTP requests in tight loop for maximum throughput
      constexpr int MAX_ITERATIONS = 500;
      for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
        webServer->handleClient();
        // Reset watchdog every 32 iterations
        if ((i & 0x1F) == 0x1F) {
          esp_task_wdt_reset();
        }
        // Yield and check for exit button every 64 iterations
        if ((i & 0x3F) == 0x3F) {
          yield();
          mappedInput.update();
          if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            activityManager.goHome();
            return;
          }
        }
      }
      lastHandleClientTime = millis();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      activityManager.goHome();
      return;
    }
  }
}

void CrossPointWebServerActivity::render(RenderLock&&) {
  if (state == WebServerActivityState::SERVER_RUNNING || state == WebServerActivityState::AP_STARTING) {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   tr(STR_HOTSPOT_MODE), nullptr);

    if (state == WebServerActivityState::SERVER_RUNNING) {
      GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                        connectedSSID.c_str());
      renderServerRunning();
    } else {
      const auto height = renderer.getLineHeight(UI_12_FONT_ID);
      const auto top = (pageHeight - height) / 2;
      renderer.drawCenteredText(UI_12_FONT_ID, top, tr(STR_STARTING_HOTSPOT));
    }
    renderer.displayBuffer();
  }
}

void CrossPointWebServerActivity::renderServerRunning() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_HOTSPOT_MODE), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    connectedSSID.c_str());

  int startY = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;
  int height10 = renderer.getLineHeight(UI_12_FONT_ID);

  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, startY, "Connect your device to", true,
                    EpdFontFamily::BOLD);
  startY += height10;
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, startY, "this WiFi network", true,
                    EpdFontFamily::BOLD);
  startY += height10 + metrics.verticalSpacing * 2;

  // Show QR code for WiFi
  const std::string wifiConfig = std::string("WIFI:S:") + connectedSSID + ";;";
  const Rect qrBoundsWifi(metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
  QrUtils::drawQrCode(renderer, qrBoundsWifi, wifiConfig);

  // Show network name
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80,
                    connectedSSID.c_str());

  startY += QR_CODE_HEIGHT + 2 * metrics.verticalSpacing;

  // Show primary URL (hostname)
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, startY, tr(STR_OPEN_URL_HINT), true,
                    EpdFontFamily::BOLD);
  startY += height10 + metrics.verticalSpacing * 2;

  std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";
  std::string ipUrl = tr(STR_OR_HTTP_PREFIX) + connectedIP + "/";

  // Show QR code for URL
  const Rect qrBoundsUrl(metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
  QrUtils::drawQrCode(renderer, qrBoundsUrl, hostnameUrl);

  // Show IP address as fallback
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 80,
                    hostnameUrl.c_str());
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding + QR_CODE_WIDTH + metrics.verticalSpacing, startY + 110,
                    ipUrl.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
