#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <random>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <stdexcept>
#include <omp.h>

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%d_%H-%M-%S");
    return oss.str();
}

struct Node {
    size_t id;
    double x;
    double y;
};

double distance(const Node& a, const Node& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double tourCost(const std::vector<size_t>& tour, const std::vector<Node>& nodes) {
    double total = 0.0;
    size_t n = tour.size();
    for (size_t i = 0; i < n; ++i) {
        total += distance(nodes[tour[i]], nodes[tour[(i + 1) % n]]);
    }
    return total;
}

void twoOptSwap(std::vector<size_t>& tour, size_t i, size_t j) {
    while (i < j) {
        std::swap(tour[i++], tour[j--]);
    }
}

struct SAParams {
    double startTemp;
    double endTemp;
    double coolingRate;
    unsigned iterPerTemp;
};

SAParams calibrateParameters(const std::vector<Node>& nodes) {
    size_t n = nodes.size();

    std::vector<size_t> tour(n);
    std::iota(tour.begin(), tour.end(), 0);

    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> indexDist(0, n - 1);

    unsigned sampleSize = static_cast<unsigned>(std::min(n * 20, size_t(5000)));
    double sumPositiveDelta = 0.0;
    double minPositiveDelta = std::numeric_limits<double>::max();
    unsigned countPositive = 0;

    for (unsigned k = 0; k < sampleSize; ++k) {
        size_t i = indexDist(rng);
        size_t j = indexDist(rng);
        if (i == j) continue;
        if (i > j) std::swap(i, j);

        size_t pi = (i - 1 + n) % n;
        size_t nj = (j + 1) % n;

        double removed = distance(nodes[tour[pi]], nodes[tour[i]])
                       + distance(nodes[tour[j]], nodes[tour[nj]]);
        double added = distance(nodes[tour[pi]], nodes[tour[j]])
                     + distance(nodes[tour[i]], nodes[tour[nj]]);
        double delta = added - removed;

        if (delta > 0.0) {
            sumPositiveDelta += delta;
            minPositiveDelta = std::min(minPositiveDelta, delta);
            ++countPositive;
        }
    }

    if (countPositive == 0) {
        std::cerr << "Calibration warning: no worsening moves found, using defaults.\n";
        return {1000.0, 0.0001, 0.995, static_cast<unsigned>(n)};
    }

    double deltaAvg = sumPositiveDelta / countPositive;

    double startTemp = -deltaAvg / std::log(0.9);

    double endTemp = -minPositiveDelta / std::log(0.0001);

    if (endTemp >= startTemp) {
        endTemp = startTemp * 1e-5;
    }

    unsigned targetSteps = 100 * static_cast<unsigned>(n);
    double coolingRate = std::exp(std::log(endTemp / startTemp) / targetSteps);
    coolingRate = std::max(0.900, std::min(0.9999, coolingRate));

    unsigned iterPerTemp = static_cast<unsigned>(n);

    std::cout << "=== Auto-tune results ===\n"
              << "  Samples       : " << sampleSize << "\n"
              << "  delta_avg     : " << deltaAvg << "\n"
              << "  delta_min     : " << minPositiveDelta << "\n"
              << "  start-temp    : " << startTemp << "\n"
              << "  end-temp      : " << endTemp << "\n"
              << "  cooling-rate  : " << coolingRate << "\n"
              << "  iter-per-temp : " << iterPerTemp << "\n"
              << "=========================\n\n";

    return {startTemp, endTemp, coolingRate, iterPerTemp};
}

std::vector<size_t> simulatedAnnealing(
    const std::vector<Node>& nodes,
    const std::filesystem::path& progressPath,
    double startTemp,
    double endTemp,
    double coolingRate,
    unsigned iterPerTemp,
    unsigned numThreads
) {
    size_t n = nodes.size();

    std::random_device rd;
    std::vector<std::vector<size_t>> threadTours(numThreads);
    std::vector<double> threadCosts(numThreads);
    std::vector<std::mt19937> threadRngs(numThreads);

    std::vector<size_t> current(n);
    std::iota(current.begin(), current.end(), 0);
    double currentCost = tourCost(current, nodes);

    for (unsigned t = 0; t < numThreads; ++t) {
        threadTours[t] = current;
        threadCosts[t] = currentCost;
        threadRngs[t].seed(rd() ^ (t + 1));
    }

    std::vector<size_t> best = current;
    double bestCost = currentCost;

    std::ofstream writer(progressPath);
    if (!writer) {
        throw std::runtime_error("Failed to open progress file: " + progressPath.string());
    }
    writer << "step,best_cost\n";

    unsigned totalSteps  = static_cast<unsigned>(std::log(endTemp / startTemp) / std::log(coolingRate));
    unsigned logInterval = (totalSteps / 50 > 0) ? (totalSteps / 50) : 1;
    unsigned step = 0;

    std::cout << "Nodes loaded : " << n << "\n"
              << "Start temp   : " << startTemp << "\n"
              << "End temp     : " << endTemp << "\n"
              << "Cooling rate : " << coolingRate << "\n"
              << "Iters / step : " << iterPerTemp << "\n"
              << "Threads      : " << numThreads << "\n"
              << "Est. steps   : " << totalSteps << "\n\n";

    std::uniform_int_distribution<size_t> indexDist(0,n - 1);
    std::uniform_real_distribution<double> probDist(0.0,1.0);

    double T = startTemp;
    while (T > endTemp) {

        #pragma omp parallel num_threads(numThreads)
        {
            int tid = omp_get_thread_num();
            std::vector<size_t>& localTour = threadTours[tid];
            double& localCost = threadCosts[tid];
            std::mt19937& localRng = threadRngs[tid];

            for (unsigned iter = 0; iter < iterPerTemp; ++iter) {
                size_t i = indexDist(localRng);
                size_t j = indexDist(localRng);
                if (i == j) continue;
                if (i > j) std::swap(i, j);

                size_t pi = (i - 1 + n) % n;
                size_t nj = (j + 1) % n;

                if (pi == j || nj == i) continue;

                double removed = distance(nodes[localTour[pi]],nodes[localTour[i]])
                               + distance(nodes[localTour[j]],nodes[localTour[nj]]);
                double added = distance(nodes[localTour[pi]],nodes[localTour[j]])
                               + distance(nodes[localTour[i]],nodes[localTour[nj]]);
                double delta = added - removed;

                if (delta < 0.0 || probDist(localRng) < std::exp(-delta / T)) {
                    twoOptSwap(localTour, i, j);
                    localCost += delta;
                }
            }
        }

        unsigned bestThread = 0;
        for (unsigned t = 1; t < numThreads; ++t) {
            if (threadCosts[t] < threadCosts[bestThread]) {
                bestThread = t;
            }
        }

        current = threadTours[bestThread];
        currentCost = threadCosts[bestThread];

        for (unsigned t = 0; t < numThreads; ++t) {
            threadTours[t] = current;
            threadCosts[t] = currentCost;
        }

        if (currentCost < bestCost) {
            best = current;
            bestCost = currentCost;
        }

        writer << step << "," << bestCost << "\n";

        if (step % logInterval == 0) {
            double progress = 100.0 * step / totalSteps;
            std::cout << std::fixed << std::setprecision(2)
                      << "[" << std::setw(6) << progress << "%]"
                      << "  T = " << std::setw(12) << std::setprecision(6) << T
                      << "  best = " << std::setprecision(2) << bestCost << "\n";
        }

        T *= coolingRate;
        ++step;
    }

    std::cout << "\nFinished. Best tour cost: " << bestCost << "\n";
    return best;
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  --start-temp    <float>   Initial temperature        (default: 1000.0)\n"
              << "  --end-temp      <float>   Final temperature          (default: 0.0001)\n"
              << "  --cooling-rate  <float>   Multiplicative cooling     (default: 0.995)\n"
              << "  --iter-per-temp <uint>    Iterations per temp step   (default: 1000)\n"
              << "  --threads       <uint>    Number of OpenMP threads   (default: 1)\n"
              << "  --auto-tune               Calibrate parameters from data (overrides temp/cooling/iter)\n"
              << "  --help                    Show this message\n";
}

int main(int argc, char* argv[]) {
    double startTemp = 1000.0;
    double endTemp = 0.0001;
    double coolingRate = 0.995;
    unsigned iterPerTemp = 1000;
    unsigned numThreads = 1;
    bool autoTune = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }

        bool hasNext = (i + 1 < argc);

        if (arg == "--start-temp" && hasNext) startTemp = std::stod(argv[++i]);
        else if (arg == "--end-temp" && hasNext) endTemp = std::stod(argv[++i]);
        else if (arg == "--cooling-rate" && hasNext) coolingRate = std::stod(argv[++i]);
        else if (arg == "--iter-per-temp" && hasNext) iterPerTemp = static_cast<unsigned>(std::stoul(argv[++i]));
        else if (arg == "--threads" && hasNext) numThreads = static_cast<unsigned>(std::stoul(argv[++i]));
        else if (arg == "--auto-tune") autoTune = true;
        else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (endTemp >= startTemp) {
        std::cerr << "Error: --end-temp must be less than --start-temp\n";
        return 1;
    }
    if (coolingRate <= 0.0 || coolingRate >= 1.0) {
        std::cerr << "Error: --cooling-rate must be in (0.0, 1.0)\n";
        return 1;
    }
    if (iterPerTemp == 0) {
        std::cerr << "Error: --iter-per-temp must be greater than 0\n";
        return 1;
    }
    if (numThreads == 0) {
        std::cerr << "Error: --threads must be greater than 0\n";
        return 1;
    }
    unsigned maxThreads = static_cast<unsigned>(omp_get_max_threads());
    if (numThreads > maxThreads) {
        std::cerr << "Error: --threads " << numThreads << " exceeds available threads (" << maxThreads << ")\n";
        return 1;
    }

    std::filesystem::path exeDir = std::filesystem::canonical(argv[0]).parent_path();
    std::filesystem::path dataPath = exeDir / "data.txt";
    std::filesystem::path resultsDir = exeDir / "results";

    if (!std::filesystem::exists(resultsDir)) {
        std::filesystem::create_directory(resultsDir);
    }

    std::ifstream dataFile(dataPath);
    if (!dataFile) {
        std::cerr << "Failed to open data file: " << dataPath << "\n";
        return 1;
    }

    std::vector<Node> nodes;
    size_t id;
    double x, y;
    while (dataFile >> id >> x >> y) {
        nodes.push_back({id, x, y});
    }
    std::cout << "Loaded " << nodes.size() << " nodes from " << dataPath.filename() << "\n\n";

    std::filesystem::path runDir = resultsDir / get_timestamp();
    std::filesystem::create_directory(runDir);

    std::filesystem::path progressPath = runDir / "sa_progress.csv";
    std::filesystem::path tourPath = runDir / "best_tour.csv";

    if (autoTune) {
        SAParams p = calibrateParameters(nodes);
        startTemp = p.startTemp;
        endTemp = p.endTemp;
        coolingRate = p.coolingRate;
        iterPerTemp = p.iterPerTemp;
    }

    std::vector<size_t> bestTour = simulatedAnnealing(nodes, progressPath, startTemp, endTemp, coolingRate, iterPerTemp, numThreads);

    std::ofstream tourFile(tourPath);
    if (!tourFile) {
        std::cerr << "Failed to open tour file: " << tourPath << "\n";
        return 1;
    }
    tourFile << "order,id,x,y\n";
    size_t order = 0;
    for (size_t idx : bestTour) {
        const Node& node = nodes[idx];
        tourFile << order++ << "," << node.id << "," << node.x << "," << node.y << "\n";
    }
    const Node& first = nodes[bestTour[0]];
    tourFile << order << "," << first.id << "," << first.x << "," << first.y << "\n";

    std::cout << "\nResults saved to: " << runDir << "\n"
              << "  sa_progress.csv  (cost history)\n"
              << "  best_tour.csv    (route coordinates)\n";

    return 0;
}