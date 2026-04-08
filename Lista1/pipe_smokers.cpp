#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <semaphore>


constexpr size_t MAX_SEM = 1024;

class FifoGate {
    std::queue<int> fifoQueue;
    std::mutex mtx;
    std::condition_variable cv;

public:
    void enter(int id, const std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(mtx);
        fifoQueue.push(id);
        cv.wait(lock, [&] { return !running || fifoQueue.front() == id; });
    }

    void exit() {
        std::unique_lock<std::mutex> lock(mtx);
        if (!fifoQueue.empty()) 
            fifoQueue.pop();
        cv.notify_all();
    }
    
    void wakeAll() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.notify_all();
    }
};

class ScreenPrinter {
    std::binary_semaphore sem{1};
    
public:
    void print(int id, const std::string& msg) {
        sem.acquire();
        std::cout << "[Smoker " << id << "] " << msg << std::endl;
        sem.release();
    }
};

void randomWait(unsigned long maxMs) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(maxMs/2, maxMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(distr(gen)));
}

struct Resources {
    std::counting_semaphore<MAX_SEM> matchboxes;
    std::counting_semaphore<MAX_SEM> tampers;
    ScreenPrinter printer;
    FifoGate matchboxQueue;
    FifoGate tamperQueue;
    
    Resources(unsigned long numMatchboxes, unsigned long numTampers) 
        : matchboxes(numMatchboxes), tampers(numTampers) {}

    ~Resources()
    {
        tampers.release();
        matchboxes.release();
    }
};

void smoker(unsigned long id, unsigned long maxWait, std::atomic<bool>& running, Resources& res) {
    while (running) {
        res.printer.print(id, "waiting for a TAMPER");
        res.tamperQueue.enter(id, running);
        if (!running) break;
        
        res.tampers.acquire();
        res.tamperQueue.exit();
        
        res.printer.print(id, ">>> tampering tobacco");
        randomWait(maxWait/2);
        
        res.tampers.release();
        res.printer.print(id, "released the TAMPER");
        
        res.printer.print(id, "waiting for a MATCHBOX");
        res.matchboxQueue.enter(id, running);
        if (!running) break;
        
        res.matchboxes.acquire();
        res.matchboxQueue.exit();
        
        res.printer.print(id, ">>> lighting the pipe");
        randomWait(maxWait/2);
        
        res.matchboxes.release();
        res.printer.print(id, "released the MATCHBOX");
        
        res.printer.print(id, "~~~ smoking the pipe ~~~");
        randomWait(maxWait);
    }
    
    res.tamperQueue.exit();
    res.matchboxQueue.exit();
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " numSmokers numTampers numMatchboxes maxWait\n";
        return 1;
    }
    
    unsigned long numSmokers, numTampers, numMatchboxes, maxWait;
    try {
        numSmokers = std::stoul(argv[1]);
        numTampers = std::stoul(argv[2]);
        numMatchboxes = std::stoul(argv[3]);
        maxWait = std::stoul(argv[4]);
    } catch (const std::exception& e) {
        std::cerr << "Invalid argument: " << e.what() << '\n';
        return 1;
    }
    
    Resources resources(numMatchboxes, numTampers);
    std::atomic<bool> running{true};
    std::vector<std::thread> smokers;
    
    for (unsigned long i = 1; i <= numSmokers; i++) {
        smokers.emplace_back(smoker, i, maxWait, std::ref(running), std::ref(resources));
    }
    
    std::cout << "Press Enter to stop...\n";
    std::cin.get();
    
    running = false;
    resources.tamperQueue.wakeAll();
    resources.matchboxQueue.wakeAll();
    
    for (auto& th : smokers) {
        th.join();
    }
    
    return 0;
}