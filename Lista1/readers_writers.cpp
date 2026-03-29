#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <random>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <json.hpp>
#include <curl/curl.h>

using namespace std::chrono_literals;
using json = nlohmann::json;

std::mutex print_mtx;
std::atomic<bool> sim_running = true;

struct Book {
    std::string content = "Empty";

    std::mutex mtx;
    std::condition_variable cv;

    std::atomic<int> active_readers = 0;
    std::atomic<int> reads_since_last_write = 0;
    std::atomic<int> writers_waiting = 0;
    std::atomic<bool> writing = false;
    std::atomic<bool> first_write_done = false;
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string getQuote() {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://dummyjson.com/quotes/random");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            try {
                auto j = json::parse(readBuffer);
                // flat object: {"id": 62, "quote": "...", "author": "..."}
                std::string quote  = j["quote"].get<std::string>();
                std::string author = j["author"].get<std::string>();
                return "\"" + quote + "\" — " + author;

            } catch (const std::exception& e) {
                return "JSON Error: " + std::string(e.what());
            }
        }
    }
    return "Connection error.";
}

int getRandom(int min, int max) {
    if (min > max) return min;
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

void writer_task(int id, size_t numBooks, unsigned long maxWait, std::vector<Book>& library) {
    while (sim_running) {

        std::string new_quote = getQuote();

        int book_idx = getRandom(0, numBooks - 1);
        Book& book = library[book_idx];

        std::unique_lock<std::mutex> lock(book.mtx);

        book.writers_waiting++;
        book.cv.wait(lock, [&book] {
            bool ratio_met = !book.first_write_done || (book.reads_since_last_write >= 3);
            return !sim_running || (!book.writing && book.active_readers == 0 && ratio_met);
        });
        if (!sim_running) { book.writers_waiting = false; book.cv.notify_all(); return; }

        book.writing = true;
        book.writers_waiting--;

        book.content = new_quote;
        {
            std::lock_guard<std::mutex> p_lock(print_mtx);
            std::cout << "[Writer " << id << "] Updated Book [" << book_idx << "]\n";
        }
        book.writing = false;
        book.first_write_done = true;
        book.reads_since_last_write = 0;

        book.cv.notify_all();
        lock.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(getRandom(10, maxWait)));
    }
}

void reader_task(int id, size_t numBooks, unsigned long maxWait, std::vector<Book>& library) {
    while(sim_running) {
        int book_idx = getRandom(0, numBooks - 1);
        Book& book = library[book_idx];
        std::unique_lock<std::mutex> lock(book.mtx);

        book.cv.wait(lock, [&book] {
            bool ratio_met = !book.first_write_done || (book.reads_since_last_write >= 3);
            return !sim_running || (book.first_write_done && !book.writing && !(book.writers_waiting && ratio_met));
        });
        if (!sim_running) return;

        book.active_readers++;
        lock.unlock();
        {
            std::lock_guard<std::mutex> p_lock(print_mtx);
            std::cout << "[Reader " << id << "] Read Book [" << book_idx << "]: " << book.content << "\n";
        }
        lock.lock();
        book.active_readers--;
        book.reads_since_last_write++;

        if (book.active_readers == 0) {
            book.cv.notify_all();
        }
        lock.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(getRandom(10, maxWait)));
    }
}

int main(int argc, char *argv[]) {

    if (argc != 6) {
        std::cerr << "Usage:\n"
                  << "./program numBooks numWriters numReaders maxWritersWait maxReadersWait" << std::endl;
        return 1;
    }

    size_t numBooks, numWriters, numReaders;
    unsigned long maxWritersWait, maxReadersWait;
    try {
        numBooks = static_cast<size_t>(std::stoul(argv[1]));
        numWriters = static_cast<size_t>(std::stoul(argv[2]));
        numReaders = static_cast<size_t>(std::stoul(argv[3]));
        maxWritersWait = std::stoul(argv[4]);
        maxReadersWait = std::stoul(argv[5]);
    }
    catch (const std::exception& e) {
        std::cerr << "Invalid numeric argument: " << e.what() << std::endl;
        return 1;
    }

    std::vector<Book> library(numBooks);
    std::vector<std::thread> writers;
    std::vector<std::thread> readers;

    for (size_t i = 0; i < numWriters; ++i)
        writers.emplace_back(writer_task, i + 1, numBooks, maxWritersWait, std::ref(library));

    for (size_t i = 0; i < numReaders; ++i)
        readers.emplace_back(reader_task, i + 1, numBooks, maxReadersWait, std::ref(library));

    std::cout << "Simulation running. Press Enter to stop...\n";
    std::cin.get();
    sim_running = false;

    for (auto& book : library) {
        std::lock_guard<std::mutex> lock(book.mtx);
        book.cv.notify_all();
    }

    for (auto& w : writers) w.join();
    for (auto& r : readers) r.join();

    std::cout << "Simulation stopped.\n";
    return 0;
}