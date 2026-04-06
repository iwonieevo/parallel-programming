#include <iostream>
#include <thread>
#include <vector>
#include <semaphore>
#include <chrono>
#include <random>
#include <atomic>
#include <string>
#include <queue>
#include <condition_variable>
#include <mutex>

static constexpr std::ptrdiff_t MAX_SEM = 1024;

class FifoGate {
    std::queue<int> fifoQueue;
    std::mutex mtx;
    std::condition_variable cv;

public:
    void enter(int id, std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(mtx);
        fifoQueue.push(id);
        cv.wait(lock, [&] { return !running || fifoQueue.front() == id; });
    }

    void exit() {
        std::unique_lock<std::mutex> lock(mtx);
        if (!fifoQueue.empty()) {
            fifoQueue.pop();
        }
        cv.notify_all();
    }
};

std::counting_semaphore<MAX_SEM>* matchboxSem   = nullptr;
std::counting_semaphore<MAX_SEM>* tamperSem = nullptr;
std::binary_semaphore* screenSem = nullptr;

FifoGate matchboxQueue;
FifoGate tamperQueue;

void print(int id, const std::string& msg) {
    screenSem->acquire();
    std::cout << "[Smoker " << id << "] " << msg << std::endl;
    screenSem->release();
}

void wait(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void smoker(int id, int maxWait, std::atomic<bool>& running) {
    while (running) {
        print(id, "waiting for a TAMPER");
        tamperQueue.enter(id, running);
        if (!running) { tamperQueue.exit(); break; }

        tamperSem->acquire();
        tamperQueue.exit();

        print(id, ">>> tampering tobacco");
        wait(maxWait);

        tamperSem->release();
        print(id, "released the TAMPER");

        print(id, "waiting for a MATCHBOX");
        matchboxQueue.enter(id, running);
        if (!running) { matchboxQueue.exit(); break; }

        matchboxSem->acquire();
        matchboxQueue.exit();

        print(id, ">>> lighting the pipe");
        wait(maxWait);

        matchboxSem->release();
        print(id, "released the MATCHBOX");

        print(id, "~~~ smoking the pipe ~~~");
        wait(maxWait);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 5) return 1;
    int numSmokers = std::stoi(argv[1]), numTampers = std::stoi(argv[2]), numMatchboxes = std::stoi(argv[3]), maxWait = std::stoi(argv[4]);

    tamperSem = new std::counting_semaphore<MAX_SEM>(numTampers);
    matchboxSem = new std::counting_semaphore<MAX_SEM>(numMatchboxes);
    screenSem = new std::binary_semaphore(1);

    std::atomic<bool> running{true};
    std::vector<std::thread> smokerThreads;

    for (int i = 1; i <= numSmokers; ++i)
        smokerThreads.emplace_back(smoker, i, maxWait, std::ref(running));

    std::cin.get();
    running = false;

    for(int i=0; i < numSmokers; ++i) {
        tamperSem->release();
        matchboxSem->release();
        tamperQueue.exit();
        matchboxQueue.exit();
    }


    for (auto& th : smokerThreads) th.join();
    return 0;
}