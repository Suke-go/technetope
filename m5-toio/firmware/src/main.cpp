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

void ShowPositionData(const CubePose &pose, bool hasPose, bool hasBattery,
                      uint8_t batteryLevel)
{
  auto &display = M5.Display;
  display.fillRect(0, kStatusAreaY, display.width(),
                   display.height() - kStatusAreaY, BLACK);
  display.setCursor(6, kStatusAreaY + 4);

  const uint32_t now_ms = millis();
  display.printf("t:%08lu ms\n", static_cast<unsigned long>(now_ms));
  M5.Log.printf("[%08lu ms][display] ",
                static_cast<unsigned long>(now_ms));
  if (hasPose)
  {
    display.printf("Cube  X:%4u  Y:%4u \n Angle:%3u, on_mat:%s\n",
                   pose.x, pose.y, pose.angle,
                   pose.on_mat ? "yes" : "no");
    M5.Log.printf("x=%u y=%u angle=%u on_mat=%s ",
                  pose.x, pose.y, pose.angle,
                  pose.on_mat ? "yes" : "no");
  }
  if (hasBattery)
  {
    display.printf("Battery: %3u%%", batteryLevel);
    M5.Log.printf("battery=%u%%", batteryLevel);
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

void HandleIdData(const ToioCoreIDData &idData, CubePose &storedPose,
                  bool &hasPoseFlag, bool &pendingDisplayFlag)
{
  if (idData.type == ToioCoreIDTypePosition)
  {
    storedPose.x = idData.position.cubePosX;
    storedPose.y = idData.position.cubePosY;
    storedPose.angle = idData.position.cubeAngleDegree;
    storedPose.on_mat = true;
  }
  else
  {
    storedPose.x = 0;
    storedPose.y = 0;
    storedPose.angle = 0;
    storedPose.on_mat = false;
  }
  hasPoseFlag = true;
  pendingDisplayFlag = true;
}

void HandleBatteryLevel(uint8_t measuredLevel, uint8_t &storedLevel,
                        bool &hasBatteryFlag, bool &pendingDisplayFlag)
{
  storedLevel = measuredLevel;
  hasBatteryFlag = true;
  pendingDisplayFlag = true;
}

void InitializeM5Hardware()
{
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  M5.Display.setRotation(3);
  DrawHeader("Scanning...");
}

std::vector<ToioCore *> ScanToioCores(uint32_t durationSec)
{
  M5.Log.println("- Scan toio core cubes");
  auto cores = toio.scan(durationSec);
  if (cores.empty())
  {
    M5.Log.println("- No toio core cube found.");
    return cores;
  }

  M5.Log.printf("- %d toio core cube(s) found.\n",
                static_cast<int>(cores.size()));
  for (size_t i = 0; i < cores.size(); ++i)
  {
    ToioCore *core = cores.at(i);
    M5.Log.printf("  %d: Addr=%s  Name=%s\n", static_cast<int>(i + 1),
                  core->getAddress().c_str(), core->getName().c_str());
  }
  return cores;
}

bool EstablishConnection(ToioCore *core)
{
  if (!core)
  {
    return false;
  }
  M5.Log.printf("- Connecting to %s (%s)\n", core->getName().c_str(),
                core->getAddress().c_str());
  if (!core->connect())
  {
    M5.Log.println("- BLE connection failed.");
    return false;
  }
  M5.Log.println("- BLE connection succeeded.");
  return true;
}

void ConfigureActiveCore(ToioCore *core)
{
  if (!core)
  {
    return;
  }

  core->setIDnotificationSettings(/*minimum_interval=*/5,
                                  /*condition=*/0x01);
  core->setIDmissedNotificationSettings(/*sensitivity=*/10);
  core->onIDReaderData([](ToioCoreIDData id_data)
                       { HandleIdData(id_data, g_latestPose, g_hasIdData,
                                      g_pendingDisplay); });
  core->onBattery([](uint8_t level)
                  { HandleBatteryLevel(level, g_batteryLevel,
                                       g_hasBatteryLevel,
                                       g_pendingDisplay); });

  g_activeCore = core;
  DrawHeader(core->getName().c_str());
  HandleBatteryLevel(core->getBatteryLevel(), g_batteryLevel,
                     g_hasBatteryLevel, g_pendingDisplay);
  HandleIdData(core->getIDReaderData(), g_latestPose, g_hasIdData,
               g_pendingDisplay);

  ShowPositionData(g_latestPose, g_hasIdData, g_hasBatteryLevel,
                   g_batteryLevel);
  g_pendingDisplay = false;
  g_lastDisplay = millis();
}

void setup()
{
  InitializeM5Hardware();

  std::vector<ToioCore *> toiocore_list = ScanToioCores(kScanDurationSec);
  if (toiocore_list.empty())
  {
    DrawHeader("No cube found.");
    return;
  }

  g_activeCore = PickTargetCore(toiocore_list);
  if (!g_activeCore)
  {
    M5.Log.printf("- Target fragment \"%s\" not matched.\n",
                  kTargetCubeNameFragment);
    DrawHeader("Target cube not found.");
    return;
  }

  if (!EstablishConnection(g_activeCore))
  {
    DrawHeader("Connection failed.");
    return;
  }

  ConfigureActiveCore(g_activeCore);

  // テスト用のLED点灯と簡易モータ動作
  g_activeCore->turnOnLed(0x00, 0xff, 0x80);
  g_activeCore->controlMotor(/*ldir=*/true, /*lspeed=*/30,
                             /*rdir=*/true, /*rspeed=*/30);
  delay(1000);
  g_activeCore->controlMotor(/*ldir=*/true, /*lspeed=*/0,
                             /*rdir=*/true, /*rspeed=*/0);
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
      ShowPositionData(g_latestPose, g_hasIdData, g_hasBatteryLevel,
                       g_batteryLevel);
      g_pendingDisplay = false;
      g_lastDisplay = now;
    }
  }

  delay(10);
}
