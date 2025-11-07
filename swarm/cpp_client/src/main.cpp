#include "toio/client/toio_client.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using toio::client::ToioClient;
using Json = nlohmann::json;

namespace {

struct Options {
  std::string host = "127.0.0.1";
  std::string port = "8765";
  std::string endpoint = "/ws";
  std::vector<std::string> cube_ids;
  bool auto_subscribe = false;
};

void print_usage(const char *argv0) {
  std::cout << "Usage: " << argv0
            << " --id <cube-id> [--id <cube-id> ...] [--host <host>] "
               "[--port <port>] [--path <endpoint>] [--subscribe]\n";
}

Options parse_options(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      opt.host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      opt.port = argv[++i];
    } else if (arg == "--path" && i + 1 < argc) {
      opt.endpoint = argv[++i];
    } else if (arg == "--id" && i + 1 < argc) {
      opt.cube_ids.push_back(argv[++i]);
    } else if (arg == "--subscribe") {
      opt.auto_subscribe = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  if (opt.cube_ids.empty()) {
    throw std::runtime_error("At least one --id <cube-id> is required");
  }

  return opt;
}

void print_help() {
  std::cout << "Commands:\n"
            << "  help                      Show this message\n"
            << "  use <cube-id>             Switch active cube\n"
            << "  connect                   Send connect command to active cube\n"
            << "  disconnect                Send disconnect command to active cube\n"
            << "  move <L> <R> [require]    Send move command (-100..100). "
               "require=0 to skip result\n"
            << "  moveall <L> <R> [require] Broadcast move to all known cubes\n"
            << "  stop                      Send move 0 0 to active cube\n"
            << "  led <R> <G> <B>           Set LED color (0-255)\n"
            << "  ledall <R> <G> <B>        Broadcast LED color to all known cubes\n"
            << "  battery                   Query battery once\n"
            << "  pos                       Query position once\n"
            << "  subscribe                 Enable position notify stream\n"
            << "  unsubscribe               Disable position notify stream\n"
            << "  exit / quit               Disconnect all cubes and exit\n";
}

std::vector<std::string> tokenize(const std::string &line) {
  std::istringstream iss(line);
  std::vector<std::string> tokens;
  std::string token;
  while (iss >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

int to_int(const std::string &value) {
  return std::stoi(value);
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options = parse_options(argc, argv);

    ToioClient client(options.host, options.port, options.endpoint);
    client.set_log_handler([](const std::string &msg) {
      std::cout << "[LOG] " << msg << std::endl;
    });
    client.set_message_handler([](const Json &json) { //受信メッセージ処理
      auto extract_target = [](const Json &obj) -> std::string { // target抽出関数
        if (obj.contains("target") && obj["target"].is_string()) {
          return obj["target"].get<std::string>();
        }
        return {};
      };

      std::string target = extract_target(json);
      if (target.empty()) { 
        auto payload_it = json.find("payload"); // payload内も確認 
        if (payload_it != json.end() && payload_it->is_object()) {
          target = extract_target(*payload_it);
        }
      }

      if (!target.empty()) {
        std::cout << "[RECV][" << target << "] " << json.dump() << std::endl;
      } else {
        std::cout << "[RECV] " << json.dump() << std::endl;
      }
    });

    client.connect();

    std::unordered_map<std::string, bool> subscriptions;
    std::unordered_set<std::string> known_cubes(options.cube_ids.begin(),
                                               options.cube_ids.end());
    std::string active_cube = options.cube_ids.front();

    for (const auto &cube_id : options.cube_ids) {
      subscriptions.emplace(cube_id, false);
      client.connect_cube(cube_id, true);
      if (options.auto_subscribe) {
        client.query_position(cube_id, true);
        subscriptions[cube_id] = true;
      }
    }

    print_help();
    auto require_active_cube = [&]() -> const std::string & {
      if (active_cube.empty()) {
        throw std::runtime_error("No active cube selected");
      }
      return active_cube;
    };

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
      auto tokens = tokenize(line);
      if (tokens.empty()) {
        continue;
      }
      const std::string &cmd = tokens.front(); // token の最初の要素がコマンド
      try {
        if (cmd == "help") {
          print_help();
        } else if (cmd == "use") {
          if (tokens.size() < 2) {
            std::cout << "Usage: use <cube-id>\n";
            continue;
          }
          const std::string &requested = tokens[1];
          if (!known_cubes.count(requested)) {
            std::cout << "Unknown cube id: " << requested << std::endl;
            continue;
          }
          active_cube = requested;
          std::cout << "Active cube set to " << active_cube << std::endl;
        } else if (cmd == "connect") {
          const std::string &target = require_active_cube();
          client.connect_cube(target, true);
        } else if (cmd == "disconnect") {
          const std::string &target = require_active_cube();
          client.disconnect_cube(target, true);
        } else if (cmd == "move" && tokens.size() >= 3) {
          const std::string &target = require_active_cube();
          int left = to_int(tokens[1]);
          int right = to_int(tokens[2]);
          std::optional<bool> req = true;
          if (tokens.size() >= 4) {
            req = tokens[3] != "0";
          }
          client.send_move(target, left, right, req);
        } else if (cmd == "moveall" && tokens.size() >= 3) {
          int left = to_int(tokens[1]);
          int right = to_int(tokens[2]);
          std::optional<bool> req = true;
          if (tokens.size() >= 4) {
            req = tokens[3] != "0";
          }
          if (subscriptions.empty()) {
            std::cout << "No cubes registered. Use 'use <cube-id>' first.\n";
            continue;
          }
          for (const auto &[cube_id, _] : subscriptions) {
            try {
              client.send_move(cube_id, left, right, req);
            } catch (const std::exception &ex) {
              std::cout << "Command error (" << cube_id
                        << "): " << ex.what() << std::endl;
            }
          }
          std::cout << "Broadcast move command to " << subscriptions.size()
                    << " cubes.\n";
        } else if (cmd == "stop") {
          const std::string &target = require_active_cube();
          client.send_move(target, 0, 0, false);
        } else if (cmd == "led" && tokens.size() >= 4) {
          const std::string &target = require_active_cube();
          int r = to_int(tokens[1]);
          int g = to_int(tokens[2]);
          int b = to_int(tokens[3]);
          client.set_led(target, r, g, b, false);
        } else if (cmd == "ledall" && tokens.size() >= 4) {
          int r = to_int(tokens[1]);
          int g = to_int(tokens[2]);
          int b = to_int(tokens[3]);
          if (subscriptions.empty()) {
            std::cout << "No cubes registered. Use 'use <cube-id>' first.\n";
            continue;
          }
          for (const auto &[cube_id, _] : subscriptions) {
            try {
              client.set_led(cube_id, r, g, b, false);
            } catch (const std::exception &ex) {
              std::cout << "LED command error (" << cube_id
                        << "): " << ex.what() << std::endl;
            }
            // 数μ秒の遅延を入れて連続送信による問題を回避
            std::this_thread::sleep_for(std::chrono::microseconds(1));
          }
          std::cout << "Broadcast LED command to " << subscriptions.size()
                    << " cubes.\n";
        } else if (cmd == "battery") {
          const std::string &target = require_active_cube();
          client.query_battery(target);
        } else if (cmd == "pos") {
          const std::string &target = require_active_cube();
          client.query_position(target, false);
        } else if (cmd == "subscribe") {
          const std::string &target = require_active_cube();
          if (subscriptions[target]) {
            std::cout << "Already subscribed to " << target << std::endl;
          } else {
            client.query_position(target, true);
            subscriptions[target] = true;
            std::cout << "Subscribed to " << target << std::endl;
          }
        } else if (cmd == "unsubscribe") {
          const std::string &target = require_active_cube();
          if (!subscriptions[target]) {
            std::cout << "Not subscribed to " << target << std::endl;
          } else {
            client.query_position(target, false);
            subscriptions[target] = false;
            std::cout << "Unsubscribed from " << target << std::endl;
          }
        } else if (cmd == "exit" || cmd == "quit") {
          break;
        } else {
          std::cout << "Unknown command. Type 'help' for options.\n";
        }
      } catch (const std::exception &ex) {
        std::cout << "Command error: " << ex.what() << std::endl;
      }
    }

    for (const auto &cube_id : options.cube_ids) {
      try {
        client.disconnect_cube(cube_id, true);
      } catch (const std::exception &ex) {
        std::cout << "Disconnect error (" << cube_id
                  << "): " << ex.what() << std::endl;
      }
    }
    client.close();
  } catch (const std::exception &ex) {
    std::cerr << "Fatal error: " << ex.what() << std::endl;
    print_usage(argv[0]);
    return 1;
  }

  return 0;
}
