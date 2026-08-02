#include <iostream>
#include <random>

int main() {
    for (int starIndex = 0; starIndex < 10; ++starIndex) {
        std::mt19937 lrng(starIndex >= 0 ? (0x9E3779B9u ^ uint32_t(starIndex)) : 0xD1B54A35u);
        int planetCount = 3 + int(lrng() % 4u);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double val = dist(lrng);
        std::cout << "Star " << starIndex << ": " << planetCount << " planets, " << val << "\n";
    }
    return 0;
}
