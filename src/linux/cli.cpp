#include "native.h"
#include <clocale>
#include <cstring>
#include <iostream>
int main(int argc, char **argv) {
  std::setlocale(LC_ALL, "");
  if (argc != 3) {
    std::cerr << "usage: lcb-cli PORT PROMPT\n";
    return 2;
  }
  try {
    size_t used = 0;
    int port = std::stoi(argv[1], &used);
    if (used != std::strlen(argv[1]) || port < 1 || port > 65535)
      throw std::runtime_error("Invalid port.");
    lcb::validate_text(argv[2], LcbMaxPrompt, false);
    Generation g = generation_defaults();
    Module m{"General", lcb_modules[0].instruction};
    Conversation c{};
    char *raw = conversation_generate(&c, &m, argv[2], &g);
    if (!raw)
      throw std::runtime_error("Invalid request.");
    std::string wire(raw);
    cJSON_free(raw);
    auto reply =
        lcb::Transport(static_cast<unsigned short>(port)).generate(wire);
    if (reply.status == "error" || reply.status == "stopped") {
      std::cerr << reply.error << '\n';
      return 1;
    }
    std::cout << reply.content << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
