#include <M5Unified.h>
#include <Toio.h>
#include <string>
#include <vector>

// toio制御用インスタンス
Toio toio;

namespace {
constexpr const char kTargetCubeNameFragment[] =
    "TARGET_NAME";  // 接続したいコアキューブ名の一部を指定
constexpr uint32_t kScanSeconds = 3;
}

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  M5.begin(cfg);

  M5.Display.setRotation(3);
  M5.Display.setTextFont(2);
  M5.Display.setTextColor(WHITE, BLACK);

  M5.Log.println("- Scan toio core cubes");
  std::vector<ToioCore*> toiocore_list = toio.scan(kScanSeconds);
  size_t n = toiocore_list.size();
  if (n == 0) {
    M5.Log.println("- Not found any toio core cubes.");
    return;
  }
  M5.Log.printf("- %d  toio core cube(s) found.\n", n);

  ToioCore* targetCore = nullptr;
  for(size_t i=0; i<n; i++) {
    ToioCore* toiocore = toiocore_list.at(i);
    M5.Log.printf("  %d: ID=%s, Name=%s\n", i+1, toiocore->getAddress().c_str(), toiocore->getName().c_str());
    if (!targetCore) {
      const std::string &name = toiocore->getName();
      if (!name.empty() &&
          name.find(kTargetCubeNameFragment) != std::string::npos) {
        targetCore = toiocore;
      }
    }
  }

  if (!targetCore) {
    M5.Log.printf("- Target name fragment \"%s\" not found. Abort.\n",
                  kTargetCubeNameFragment);
    return;
  }

  M5.Log.println("- Establish BLE connection to toio core cube.");
  ToioCore* toiocore = targetCore;
  bool connected = toiocore->connect();
  if (!connected) {
    M5.Log.println("- BLE connection failed.");
    return;
  }
  M5.Log.println("- BLE connection was succeeded.");
  M5.Log.println("- Disconnect after 3 seconds.");
  delay(3000);
  toiocore->disconnect();
  M5.Log.println("- BLE connection was disconnected.");
}

void loop() {

}
