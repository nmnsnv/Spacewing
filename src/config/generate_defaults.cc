#include "config.h"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: spacewing-config-defaults OUTPUT\n";
        return 64;
    }
    std::ofstream output(argv[1]);
    output << spacewing::defaultConfig();
    output.close();
    if (!output) {
        std::cerr << "Cannot write default config: " << argv[1] << '\n';
        return 1;
    }
}
