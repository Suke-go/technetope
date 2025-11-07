#include "toio/client/toio_client.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using toio::client::ToioClient;
using Json = nlohmann::json;

namespace {

struct Options {
  std::string host = "127.0.0.1";
  std::string port = "8765";
  std::string endpoint = "/ws";
  std::string cube_id;
  bool auto_subscribe = false;
};

void print_usage(const char *argv0) {
  std::cout << "Usage: " << argv0
            << " --id <cube-id> [--host <host>] [--port <port>] "
               "[--path <endpoint>] [--subscribe]\n";
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
      opt.cube_id = argv[++i];
    } else if (arg == "--subscribe") {
      opt.auto_subscribe = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  if (opt.cube_id.empty()) {
    throw std::runtime_error("--id <cube-id> is required");
  }

  return opt;
}

void print_help() {
  std::cout << "Commands:\n"
            << "  help                      Show this message\n"
            << "  connect                   Send connect command\n"
            << "  disconnect                Send disconnect command\n"
            << "  move <L> <R> [require]    Send move command (-100..100). "
               "require=0 to skip result\n"
            << "  stop                      Send move 0 0\n"
            << "  led <R> <G> <B>           Set LED color (0-255)\n"
            << "  battery                   Query battery once\n"
            << "  pos                       Query position once\n"
            << "  subscribe                 Enable position notify stream\n"
            << "  unsubscribe               Disable position notify stream\n"
            << "  exit / quit               Disconnect and exit\n";
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
    client.set_message_handler([](const Json &json) {
      std::cout << "[RECV] " << json.dump() << std::endl;
    });

    client.connect();
    client.connect_cube(options.cube_id, true);
    if (options.auto_subscribe) {
      client.query_position(options.cube_id, true);
    }

    print_help();
    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
      auto tokens = tokenize(line);
      if (tokens.empty()) {
        continue;
      }
      const std::string &cmd = tokens.front();
      try {
        if (cmd == "help") {
          print_help();
        } else if (cmd == "connect") {
          client.connect_cube(options.cube_id, true);
        } else if (cmd == "disconnect") {
          client.disconnect_cube(options.cube_id, true);
        } else if (cmd == "move" && tokens.size() >= 3) {
          int left = to_int(tokens[1]);
          int right = to_int(tokens[2]);
          std::optional<bool> req = true;
          if (tokens.size() >= 4) {
            req = tokens[3] != "0";
          }
          client.send_move(options.cube_id, left, right, req);
        } else if (cmd == "stop") {
          client.send_move(options.cube_id, 0, 0, false);
        } else if (cmd == "led" && tokens.size() >= 4) {
          int r = to_int(tokens[1]);
          int g = to_int(tokens[2]);
          int b = to_int(tokens[3]);
          client.set_led(options.cube_id, r, g, b, false);
        } else if (cmd == "battery") {
          client.query_battery(options.cube_id);
        } else if (cmd == "pos") {
          client.query_position(options.cube_id, false);
        } else if (cmd == "subscribe") {
          client.query_position(options.cube_id, true);
        } else if (cmd == "unsubscribe") {
          client.query_position(options.cube_id, false);
        } else if (cmd == "exit" || cmd == "quit") {
          break;
        } else {
          std::cout << "Unknown command. Type 'help' for options.\n";
        }
      } catch (const std::exception &ex) {
        std::cout << "Command error: " << ex.what() << std::endl;
      }
    }

    client.disconnect_cube(options.cube_id, true);
    client.close();
  } catch (const std::exception &ex) {
    std::cerr << "Fatal error: " << ex.what() << std::endl;
    print_usage(argv[0]);
    return 1;
  }

  return 0;
}
