#include <M5Unified.h>
#include <Toio.h>
#include <cstring>
#include <string>
#include <vector>

namespace
{
  constexpr const char kTargetCubeNameFragment[] =
      "38t"; // 接続したいコアキューブ名の一部（空文字列なら先頭に接続）
  constexpr uint32_t kScanDurationSec = 3;
  constexpr uint32_t kRefreshIntervalMs = 1000;
  constexpr uint32_t kStatusAreaY = 40;

  struct CubePose
  {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t angle = 0;
    bool on_mat = false;
  };

  Toio toio;
  ToioCore *g_activeCore = nullptr;
  CubePose g_latestPose{};
  bool g_hasIdData = false;
  bool g_pendingDisplay = false;
  uint32_t g_lastDisplay = 0;
  uint8_t g_batteryLevel = 0;
  bool g_hasBatteryLevel = false;
}

void DrawHeader(const char *message)
{
  auto &display = M5.Display;
  display.fillScreen(BLACK);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(WHITE, BLACK);
  display.setTextSize(2);
  display.drawString("toio position monitor", display.width() / 2, 14);
  display.setTextSize(1);
  display.drawString(message, display.width() / 2, 30);
  display.setTextDatum(TL_DATUM);
}

void ShowPositionData(const CubePose &pose)
{
  auto &display = M5.Display;
  display.fillRect(0, kStatusAreaY, display.width(),
                   display.height() - kStatusAreaY, BLACK);
  display.setCursor(6, kStatusAreaY + 4);

  const uint32_t now_ms = millis();
  display.printf("t:%08lu ms\n", static_cast<unsigned long>(now_ms));
  M5.Log.printf("[%08lu ms][display] ", static_cast<unsigned long>(now_ms));
  if (g_hasIdData)
  {
    display.printf("Cube  X:%4u  Y:%4u \n Angle:%3u, on_mat:%s\n",
                   pose.x, pose.y, pose.angle,
                   pose.on_mat ? "yes" : "no");
    M5.Log.printf("x=%u y=%u angle=%u on_mat=%s ",
                  pose.x, pose.y, pose.angle,
                  pose.on_mat ? "yes" : "no");
  }
  if (g_hasBatteryLevel)
  {
    display.printf("Battery: %3u%%", g_batteryLevel);
    M5.Log.printf("battery=%u%%", g_batteryLevel);
  }
  M5.Log.println();
}

ToioCore *PickTargetCore(const std::vector<ToioCore *> &cores)
{
  if (cores.empty())
  {
    return nullptr;
  }
  if (std::strlen(kTargetCubeNameFragment) == 0)
  {
    return cores.front();
  }
  for (auto *core : cores)
  {
    const std::string &name = core->getName();
    if (name.find(kTargetCubeNameFragment) != std::string::npos)
    {
      return core;
    }
  }
  return nullptr;
}

void HandleIdData(const ToioCoreIDData &data)
{
  if (data.type == ToioCoreIDTypePosition)
  {
    g_latestPose.x = data.position.cubePosX;
    g_latestPose.y = data.position.cubePosY;
    g_latestPose.angle = data.position.cubeAngleDegree;
    g_latestPose.on_mat = true;
  }
  else
  {
    g_latestPose.x = 0;
    g_latestPose.y = 0;
    g_latestPose.angle = 0;
    g_latestPose.on_mat = false;
  }
  g_hasIdData = true;
  g_pendingDisplay = true;
}

void HandleBatteryLevel(uint8_t level)
{
  g_batteryLevel = level;
  g_hasBatteryLevel = true;
  g_pendingDisplay = true;
}

void setup()
{
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  M5.Display.setRotation(3);
  DrawHeader("Scanning...");

  M5.Log.println("- Scan toio core cubes");
  std::vector<ToioCore *> toiocore_list = toio.scan(kScanDurationSec);
  if (toiocore_list.empty())
  {
    M5.Log.println("- No toio core cube found.");
    DrawHeader("No cube found.");
    return;
  }

  M5.Log.printf("- %d toio core cube(s) found.\n",
                static_cast<int>(toiocore_list.size()));
  for (size_t i = 0; i < toiocore_list.size(); ++i)
  {
    ToioCore *core = toiocore_list.at(i);
    M5.Log.printf("  %d: Addr=%s  Name=%s\n", static_cast<int>(i + 1),
                  core->getAddress().c_str(), core->getName().c_str());
  }

  ToioCore *targetCore = PickTargetCore(toiocore_list);
  if (!targetCore)
  {
    M5.Log.printf("- Target fragment \"%s\" not matched.\n",
                  kTargetCubeNameFragment);
    DrawHeader("Target cube not found.");
    return;
  }

  M5.Log.printf("- Connecting to %s (%s)\n", targetCore->getName().c_str(),
                targetCore->getAddress().c_str());
  if (!targetCore->connect())
  {
    M5.Log.println("- BLE connection failed.");
    DrawHeader("Connection failed.");
    return;
  }

  targetCore->setIDnotificationSettings(/*minimum_interval=*/5,
                                        /*condition=*/0x01);
  targetCore->setIDmissedNotificationSettings(/*sensitivity=*/10);
  targetCore->onIDReaderData([](ToioCoreIDData id_data)
                             { HandleIdData(id_data); });
  targetCore->onBattery([](uint8_t level)
                        { HandleBatteryLevel(level); });

  g_activeCore = targetCore;
  DrawHeader(targetCore->getName().c_str());
  HandleBatteryLevel(targetCore->getBatteryLevel());
  HandleIdData(targetCore->getIDReaderData());
  ShowPositionData(g_latestPose);
  g_pendingDisplay = false;
  g_lastDisplay = millis();
  M5.Log.println("- BLE connection succeeded.");
}

void loop()
{
  M5.update();
  toio.loop();

  if (!g_activeCore)
  { // No active core
    delay(100);
    return;
  }

  if (g_hasIdData)
  { // New ID data available
    const uint32_t now = millis();
    if (g_pendingDisplay || (now - g_lastDisplay) >= kRefreshIntervalMs)
    {
      ShowPositionData(g_latestPose);
      g_pendingDisplay = false;
      g_lastDisplay = now;
    }
  }

  delay(10);
}
