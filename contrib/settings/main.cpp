#include "Settings.hpp"
#include <iostream>
#include <unistd.h>
int main(int argc, char **argv) {
    bool reveal = argc == 3 && std::string(argv[2]) == "--reveal";
    if (argc < 2 || argc > 3 || (argc == 3 && !reveal)) {
        std::cerr << "Usage: istat-settings-read opt|local [--reveal]\n";
        return 2;
    }
    // Do not make this binary setuid or grant it a broad sudoers exception.
    std::string result = istat_settings::read(argv[1], reveal);
    std::cout << result << '\n';
    return result.find("\"state\":\"readable\"") != std::string::npos ? 0 : 1;
}
