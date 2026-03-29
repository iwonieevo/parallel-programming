#include <iostream>
#include <string>
#include <random>
#include <vector>
#include <ctime>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <curses.h>
#include <json.hpp>

using namespace std::chrono_literals;
using json = nlohmann::json;
using std::string, std::vector, std::thread, std::mutex, std::condition_variable, std::atomic;

struct Book
{
    mutex mtx;
    condition_variable cv;
    
    
    const string representation;
    string mostRecentQuote;
    atomic<unsigned int> readCount;
    atomic<unsigned int> activeReaders;
    atomic<unsigned int> writersWaiting;

    Book(string repr) : mostRecentQuote(""), representation(repr), readCount(0), activeReaders(0), writersWaiting(0) {}

    bool can_write(void) const { return activeReaders == 0 && (mostRecentQuote.empty() || readCount >= 3); }
    bool can_read(void) const { return !mostRecentQuote.empty() && (writersWaiting == 0 || readCount < 3); }
};

struct Library
{
    size_t numBooks;
    vector<std::unique_ptr<Book>> books;

    Library(size_t n) : numBooks(n) { for (size_t i = 0; i < n; i++) { books.emplace_back(std::make_unique<Book>("Book #" + std::to_string(i))); } }

    Book* get_rand_book(void);
};

enum class State : uint8_t
{
    ACTIVE,
    WAITING,
    RESTING,
    STATE_COUNT
};

class Person
{
protected:
    thread thd;
    mutable mutex mtx;
    Library* library;
    Book* currentBook;
    unsigned long minDuration, maxDuration, minCooldown, maxCooldown;
    atomic<bool> active;
    atomic<bool>& simRunning;

    virtual void task(void) = 0;
    void sleep_random(unsigned long minSleep, unsigned long maxSleep);
    void start(void) { thd = thread(&Person::task, this); }

public:
    Person(Library* lib, unsigned long minD, unsigned long maxD, unsigned long minC, unsigned long maxC, atomic<bool> &simRun) 
    : library(lib), minDuration(minD), maxDuration(maxD), minCooldown(minC), maxCooldown(maxC), simRunning(simRun),
      currentBook(nullptr), active(false) {}
    virtual ~Person() { if (thd.joinable()) thd.join(); }

    State get_state(void) const;
    string get_current_book_repr(void) const { std::lock_guard person_lock(mtx); return currentBook ? currentBook->representation : "None"; }
};

class Writer : public Person
{
    string getQuote(void) { return "Quote at " + std::to_string(std::time(nullptr)); }
    void task(void) override;

public:
    Writer(Library* lib, unsigned long minD, unsigned long maxD, unsigned long minC, unsigned long maxC, atomic<bool> &simRun)
    : Person(lib, minD, maxD, minC, maxC, simRun) { start(); }
};

class Reader : public Person
{
    void task(void) override;

public:
    Reader(Library* lib, unsigned long minD, unsigned long maxD, unsigned long minC, unsigned long maxC, atomic<bool> &simRun)
    : Person(lib, minD, maxD, minC, maxC, simRun) { start(); }
};

void display_loop(vector<std::unique_ptr<Reader>>& readers, vector<std::unique_ptr<Writer>>& writers, Library& library, atomic<bool>& simRunning);

int main(int argc, char *argv[])
{
    // defaults
    size_t numBooks   = 3;
    size_t numReaders = 5;
    size_t numWriters = 2;

    unsigned long readerMinDuration = 1000, readerMaxDuration = 3000;
    unsigned long readerMinCooldown = 500,  readerMaxCooldown = 2000;
    unsigned long writerMinDuration = 1000, writerMaxDuration = 4000;
    unsigned long writerMinCooldown = 2000, writerMaxCooldown = 6000;

    // parse --key=value args
    auto getArg = [&](const string& key, unsigned long fallback) -> unsigned long {
        string prefix = "--" + key + "=";
        for (int i = 1; i < argc; i++)
        {
            string arg(argv[i]);
            if (arg.rfind(prefix, 0) == 0)
            {
                try { return std::stoul(arg.substr(prefix.size())); }
                catch (...) { break; }
            }
        }
        return fallback;
    };

    numBooks   = getArg("books",    numBooks);
    numReaders = getArg("readers",  numReaders);
    numWriters = getArg("writers",  numWriters);

    readerMinDuration = getArg("rmin-dur",  readerMinDuration);
    readerMaxDuration = getArg("rmax-dur",  readerMaxDuration);
    readerMinCooldown = getArg("rmin-cool", readerMinCooldown);
    readerMaxCooldown = getArg("rmax-cool", readerMaxCooldown);
    writerMinDuration = getArg("wmin-dur",  writerMinDuration);
    writerMaxDuration = getArg("wmax-dur",  writerMaxDuration);
    writerMinCooldown = getArg("wmin-cool", writerMinCooldown);
    writerMaxCooldown = getArg("wmax-cool", writerMaxCooldown);

    // clamp min <= max
    readerMaxDuration = std::max(readerMinDuration, readerMaxDuration);
    readerMaxCooldown = std::max(readerMinCooldown, readerMaxCooldown);
    writerMaxDuration = std::max(writerMinDuration, writerMaxDuration);
    writerMaxCooldown = std::max(writerMinCooldown, writerMaxCooldown);

    atomic<bool> simRunning(true);
    Library library(numBooks);

    vector<std::unique_ptr<Reader>> readers;
    vector<std::unique_ptr<Writer>> writers;

    readers.reserve(numReaders);
    writers.reserve(numWriters);

    for (size_t i = 0; i < numReaders; i++) { readers.emplace_back(std::make_unique<Reader>(&library, readerMinDuration, readerMaxDuration, readerMinCooldown, readerMaxCooldown, simRunning)); }
        
    for (size_t i = 0; i < numWriters; i++) { writers.emplace_back(std::make_unique<Writer>(&library, writerMinDuration, writerMaxDuration, writerMinCooldown, writerMaxCooldown, simRunning)); }
        
    display_loop(readers, writers, library, simRunning);

    // display_loop sets simRunning=false and returns - wake all blocked threads
    for (auto& book : library.books) { book->cv.notify_all(); }

    // destructors of Reader/Writer join threads
    readers.clear();
    writers.clear();

    return 0;
}


Book* Library::get_rand_book(void)
{
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(0, numBooks - 1);
    return books[dist(gen)].get();
}

void Person::sleep_random(unsigned long minSleep, unsigned long maxSleep)
{
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(minSleep, maxSleep);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
}

State Person::get_state(void) const
{
    std::lock_guard person_lock(mtx);
    if (currentBook == nullptr) { return State::RESTING; }
    else if (active) { return State::ACTIVE; }
    else { return State::WAITING; }
}

void Writer::task(void)
{
    while (simRunning)
    {
        string newQuote = getQuote();

        { std::lock_guard<std::mutex> person_lock(mtx); currentBook = library->get_rand_book(); }
        active = false;
        currentBook->writersWaiting++;

        {    
            std::unique_lock<std::mutex> book_lock(currentBook->mtx);
            currentBook->cv.wait(book_lock, [this] { return currentBook->can_write() || !simRunning; });
            currentBook->writersWaiting--;
            if (!simRunning) { currentBook->cv.notify_all(); return; }

            currentBook->mostRecentQuote = std::move(newQuote);
            currentBook->readCount = 0;
            active = true;

            sleep_random(minDuration, maxDuration);
        }

        currentBook->cv.notify_all();
        active = false;
        { std::lock_guard<std::mutex> person_lock(mtx); currentBook = nullptr; }

        sleep_random(minCooldown, maxCooldown);
    }
}

void Reader::task(void)
{
    while (simRunning)
    {
        { std::lock_guard<std::mutex> person_lock(mtx); currentBook = library->get_rand_book(); }
        active = false;

        {
            std::unique_lock<std::mutex> book_lock(currentBook->mtx);
            currentBook->cv.wait(book_lock, [this] { return currentBook->can_read() || !simRunning; });
            if (!simRunning) { currentBook->cv.notify_all(); return; }

            currentBook->activeReaders++;
            active = true;

        }

        sleep_random(minDuration, maxDuration);

        {
            std::unique_lock<std::mutex> book_lock(currentBook->mtx);
            currentBook->activeReaders--;
            currentBook->readCount++;
        }

        currentBook->cv.notify_all();
        active = false;
        { std::lock_guard<std::mutex> person_lock(mtx); currentBook = nullptr; }

        sleep_random(minCooldown, maxCooldown);
    }
}

void display_loop(vector<std::unique_ptr<Reader>>& readers, vector<std::unique_ptr<Writer>>& writers, Library& library, atomic<bool>& simRunning)
{
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors())
    {
        start_color();
        init_pair((short)State::ACTIVE  + 1, COLOR_GREEN,  COLOR_BLACK);
        init_pair((short)State::WAITING + 1, COLOR_YELLOW, COLOR_BLACK);
        init_pair((short)State::RESTING + 1, COLOR_RED,    COLOR_BLACK);
    }

    while (simRunning)
    {
        erase();
        int row = 0;
        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);
        (void)maxY;

        // --- Writers ---
        mvprintw(row++, 0, "=== WRITERS ===");
        for (size_t i = 0; i < writers.size(); i++)
        {
            State s = writers[i]->get_state();
            string repr = writers[i]->get_current_book_repr();
            string label = "Writer #" + std::to_string(i) + " [" + (s == State::RESTING ? "Resting" : (s == State::ACTIVE ? "Writing: " : "Waiting: ") + repr) + "]";
            attron(COLOR_PAIR((short)s + 1));
            mvprintw(row++, 2, "%s", label.c_str());
            attroff(COLOR_PAIR((short)s + 1));
        }

        row++;

        // --- Readers ---
        mvprintw(row++, 0, "=== READERS ===");
        for (size_t i = 0; i < readers.size(); i++)
        {
            State s = readers[i]->get_state();
            string repr = readers[i]->get_current_book_repr();
            string label = "Reader #" + std::to_string(i) + " [" + (s == State::RESTING ? "Resting" : (s == State::ACTIVE ? "Reading: " : "Waiting: ") + repr) + "]";
            attron(COLOR_PAIR((short)s + 1));
            mvprintw(row++, 2, "%s", label.c_str());
            attroff(COLOR_PAIR((short)s + 1));
        }

        row++;

        // --- Books ---
        mvprintw(row++, 0, "=== BOOKS ===");
        for (size_t i = 0; i < library.numBooks; i++)
        {
            Book* book = library.books[i].get();
            string quote = book->mostRecentQuote;
            if (quote.empty()) quote = "(empty)";

            // truncate quote to fit terminal width
            int maxQuote = maxX - 6;
            if ((int)quote.size() > maxQuote) quote = quote.substr(0, maxQuote - 3) + "...";

            mvprintw(row++, 2, "%s | reads: %u  active: %u  waiting writers: %u",
                book->representation.c_str(),
                book->readCount.load(),
                book->activeReaders.load(),
                book->writersWaiting.load());
            mvprintw(row++, 4, "%s", quote.c_str());
        }

        row++;
        mvprintw(row++, 0, "Press any key to exit...");

        refresh();

        if (getch() != ERR) break;

        std::this_thread::sleep_for(150ms);
    }

    simRunning = false;

    nodelay(stdscr, FALSE);
    mvprintw(LINES - 1, 0, "Simulation ended. Press any key...");
    refresh();
    getch();

    endwin();
}
