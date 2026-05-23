#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <random>
#include <ctime>

std::string get_timestamp()
{
    auto now = std::time(nullptr);
    char buf[sizeof("YYYY-MM-DD  HH:MM:SS")];
    return std::string(buf,buf +
        std::strftime(buf,sizeof(buf),"%F  %T",std::gmtime(&now)));
}

class Node {
public:
    int id;
    double x;
    double y;
};

double distance(const Node& a, const Node& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double tourCost(const std::vector<int>& tour, const std::vector<Node>& nodes) {
    double total = 0;
    int n = tour.size();
    for (int i = 0; i < n; ++i) {
        total += distance(nodes[tour[i]], nodes[tour[(i + 1) % n]]);
    }
    return total;
}

void twoOptSwap(std::vector<int>& tour, int i, int j) {
    std::reverse(tour.begin() + i, tour.begin() + j + 1);
}

std::vector<int> simulatedAnnealing(
    const std::vector<Node>& nodes,
    double startTemp = 1000.0,
    double endTemp = 0.0001,
    double coolingRate = 0.995,
    int iterPerTemp = 1000
) {
    int n = nodes.size();

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution indexDist(0, n - 1);
    std::uniform_real_distribution probDist(0.0, 1.0);

    std::vector<int> current(n);
    std::iota(current.begin(), current.end(), 0);

    double currentCost = tourCost(current, nodes);

    std::vector<int> best = current;
    double bestCost = currentCost;

    double T = startTemp;
    std::ofstream writer("/Users/oskarmulcan/Desktop/KZW/sa/results/" + get_timestamp() + ".txt");

    while (T > endTemp) {
        for (int iter = 0; iter < iterPerTemp; iter++) {
            int i = indexDist(rng);
            int j = indexDist(rng);
            if (i == j) continue;
            if (i > j) std::swap(i, j);

            std::vector<int> neighbour = current;
            twoOptSwap(neighbour, i, j);
            double neighbourCost = tourCost(neighbour, nodes);

            double delta = neighbourCost - currentCost;

            if (delta < 0 || probDist(rng) < std::exp(-delta / T)) {
                current = neighbour;
                currentCost = neighbourCost;

                if (currentCost < bestCost) {
                    best = current;
                    bestCost = currentCost;
                }
                std::cout << bestCost << std::endl;
            }
            writer << bestCost << std::endl;
        }
        T *= coolingRate;
    }
    std::cout << "Best tour cost: " << bestCost << std::endl;
    return best;
}

int main() {
    std::cout << std::filesystem::current_path() << std::endl;
    std::ifstream data("/Users/oskarmulcan/Desktop/KZW/sa/data.txt");
    std::cout << data.good() << std::endl;
    int id;
    double x, y;
    std::vector<Node> nodes;
    const int N = 25;
    nodes.reserve(N);
    while (data >> id >> x >> y) {
        nodes.emplace_back(Node{id, x, y});
    }
    double total_dist = 0;
    for (int i = 1; i < nodes.size(); i++) {
        total_dist += distance(nodes[i], nodes[i - 1]);
    }
    total_dist += distance(nodes.back(), nodes.front());
    std::cout << total_dist << std::endl;

    std::vector<int> bestTour = simulatedAnnealing(nodes);
    for (int i = 1; i < nodes.size(); i++) {
        std::cout << bestTour[i] << std::endl;
    }



}