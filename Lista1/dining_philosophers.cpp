#include <iostream>
#include <random>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <curses.h>

using namespace std::chrono_literals;
using std::string, std::mutex, std::thread, std::vector, std::atomic, std::unique_ptr;

enum State
{
    THINKING,
    WAITING,
    EATING,
    STATE_COUNT
};

atomic<bool> sim_running = true;

class Philosopher;

struct Fork
{
    Philosopher *owner_ptr = nullptr;
    atomic<bool> dirty     = true;
    atomic<bool> requested = false;
    mutex        fork_mtx;
};

class Philosopher
{
    int min_think, max_think;
    int min_eat, max_eat;

    thread thd;
    mutex state_mtx;

    Philosopher *l_neighbor, *r_neighbor;
    Fork *l_fork, *r_fork;

    atomic<bool> stop_requested;
    atomic<State> curr_state;

    std::chrono::steady_clock::time_point curr_state_start;
    std::chrono::milliseconds state_times[STATE_COUNT];

    std::chrono::milliseconds random_time(int, int);
    void change_state(State);
    bool check_forks(void);
    void routine(void);

public:
    Philosopher(Fork*, Fork*, int, int, int, int);

    void start(void);
    void stop(void);
    void thd_join(void);
    void set_neighbors(Philosopher*, Philosopher*);
    State get_state(void);
    std::chrono::milliseconds get_state_time(State);
};

string state_to_string(State);
void display_loop(vector<unique_ptr<Philosopher>>&);

int main(int argc, char *argv[])
{
    // Parse and validate command line arguments
    if (argc != 6)
    {
        std::cerr << "Usage:\n"
                  << "./program N minThink maxThink minEat maxEat" << std::endl;
        return 1;
    }

    size_t N;
    unsigned long minThink, maxThink, minEat, maxEat;

    try
    {
        N        = static_cast<size_t>(std::stoul(argv[1]));
        minThink = std::stoul(argv[2]);
        maxThink = std::stoul(argv[3]);
        minEat   = std::stoul(argv[4]);
        maxEat   = std::stoul(argv[5]);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Invalid numeric argument: " << e.what() << std::endl;
        return 1;
    }

    if (N < 2)
    {
        std::cerr << "There must be at least 2 philosophers." << std::endl;
        return 1;
    }

    if (minThink > maxThink || minEat > maxEat)
    {
        std::cerr << "Min times must be <= max times." << std::endl;
        return 1;
    }

    // Initialize philosophers and forks
    vector<Fork>                    forks(N);
    vector<unique_ptr<Philosopher>> philosophers;
    philosophers.reserve(N);

    for (size_t i = 0; i < N; i++)
    {
        philosophers.emplace_back
        (
                std::make_unique<Philosopher>
                (
                        &forks[i],
                        &forks[(i + 1) % N],
                        minThink, maxThink,
                        minEat, maxEat
                )
        );
    }

    for (size_t i = 0; i < N; i++)
    {
        auto& p = philosophers[i];
        p->set_neighbors(philosophers[(i - 1 + N) % N].get(), philosophers[(i + 1) % N].get());
    }

    // Assign fork to the philosopher with the smaller ID (the only case where that happens is for the last fork, which is owned by philosopher 0 instead of philosopher N-1)
    for (size_t i = 0; i < N; i++) { forks[i].owner_ptr = philosophers[(i + 1) % N == 0 ? 0 : i].get(); }

    // Start philosopher threads
    for (auto& p : philosophers) { p->start(); }

    // Start display thread
    thread display(display_loop, ref(philosophers));

    // Join philosopher threads
    for (auto& p : philosophers) { p->thd_join(); }

    // Join display thread
    if (display.joinable()) { display.join(); }

    return 0;
}

string state_to_string(State _s)
{
    switch (_s)
    {
        case THINKING:
            return "Thinking";
        case WAITING:
            return "Waiting ";
        case EATING:
            return "Eating  ";
        default:
            return "Unknown ";
    }
}

void display_loop(vector<unique_ptr<Philosopher>>& _philosophers)
{
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors())
    {
        start_color();
        init_pair(THINKING + 1, COLOR_CYAN,   COLOR_BLACK);
        init_pair(WAITING  + 1, COLOR_YELLOW, COLOR_BLACK);
        init_pair(EATING   + 1, COLOR_GREEN,  COLOR_BLACK);
    }

    while (sim_running)
    {
        erase();

        mvprintw(0,0,"Dining Philosophers Simulation (Press ESC to stop)");

        for (size_t i = 0; i < _philosophers.size(); i++)
        {
            auto& p     = _philosophers[i];
            State state = p->get_state();

            if (has_colors()) { attron(COLOR_PAIR(state + 1)); }

            mvprintw(2+i,0,"Philosopher %zu : %-7s | T: %4lldms | W: %4lldms | E: %4lldms",
                     i,
                     state_to_string(state).c_str(),
                     (long long)p->get_state_time(THINKING).count(),
                     (long long)p->get_state_time(WAITING).count(),
                     (long long)p->get_state_time(EATING).count());

            if (has_colors()) { attroff(COLOR_PAIR(state + 1)); }
        }

        refresh();

        std::this_thread::sleep_for(100ms);

        if (getch() == 27) // ESC pressed
        {
            sim_running = false;
            for (auto& p : _philosophers) { p->stop(); }
        }
    }

    mvprintw(_philosophers.size()+3,0,"Press any key to exit...");
    refresh();

    nodelay(stdscr, FALSE);
    getch();

    endwin();
}

void Philosopher::routine(void)
{
    curr_state_start = std::chrono::steady_clock::now();
    while (!stop_requested)
    {
        change_state(THINKING);
        std::this_thread::sleep_for(random_time(min_think, max_think));

        if (stop_requested) { break; }

        change_state(WAITING);
        while (!check_forks() && !stop_requested) { std::this_thread::sleep_for(5ms); }

        if (stop_requested) { break; }

        change_state(EATING);
        std::this_thread::sleep_for(random_time(min_eat, max_eat));

        // lambda for releasing a single fork
        auto release_fork = [](Fork* _fork, Philosopher* _neighbor)
        {
            std::lock_guard<std::mutex> lock(_fork->fork_mtx);
            _fork->dirty  = true;
            if (_fork->requested)
            {
                _fork->requested = false;
                _fork->owner_ptr = _neighbor;
                _fork->dirty     = false;
            }
        };

        release_fork(l_fork, l_neighbor);
        release_fork(r_fork, r_neighbor);
    }
}

void Philosopher::change_state(State _new_state)
{
    std::lock_guard<mutex> lock(state_mtx);
    if (curr_state != _new_state)
    {
        auto now = std::chrono::steady_clock::now();
        state_times[curr_state] += std::chrono::duration_cast<std::chrono::milliseconds>(now - curr_state_start);

        curr_state = _new_state;
        curr_state_start = now;
    }
}

bool Philosopher::check_forks(void)
{
    std::unique_lock l_fork_lock(l_fork->fork_mtx, std::try_to_lock);
    std::unique_lock r_fork_lock(r_fork->fork_mtx, std::try_to_lock);

    // lambda for acquirying a single fork
    auto try_acquire_fork = [this](Fork* _fork, std::unique_lock<std::mutex>& _lock) -> bool
    {
        // If we don't own the lock, we can't safely check or modify the fork's state
        if (!_lock.owns_lock()) { return false; }

        // If we already own the fork, we can continue using it
        if (_fork->owner_ptr == this) { return true; }

        // If the fork is dirty, we can acquire it (when we acquire, we clean the fork)
        if(_fork->dirty)
        {
            _fork->owner_ptr = this;
            _fork->dirty     = false;
            return true;
        }

        // Otherwise, the fork isn't our and we can't acquire it right now
        _fork->requested = true;
        _lock.unlock();
        return false;
    };

    bool l_acquired = try_acquire_fork(l_fork, l_fork_lock);
    bool r_acquired = try_acquire_fork(r_fork, r_fork_lock);

    // if we acquired both forks - we clean them and eat
    if (l_acquired && r_acquired)
    {
        l_fork->dirty = false;
        r_fork->dirty = false;
        return true;
    }

    // otherwise we must try again later
    return false;
}

void Philosopher::start(void) { thd = thread(&Philosopher::routine, this); }

void Philosopher::stop(void) { stop_requested = true; }

void Philosopher::thd_join(void) { if (thd.joinable()) { thd.join(); } }

std::chrono::milliseconds Philosopher::random_time(int _min, int _max)
{
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(_min, _max);
    return std::chrono::milliseconds(dist(gen));
}

std::chrono::milliseconds Philosopher::get_state_time(State _state)
{
    std::lock_guard<mutex> lock(state_mtx);
    auto time = state_times[_state];
    if (curr_state == _state) { time += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - curr_state_start); }

    return time;
}

State Philosopher::get_state(void)
{
    std::lock_guard<mutex> lock(state_mtx);
    return curr_state;
}

void Philosopher::set_neighbors(Philosopher* _left, Philosopher* _right)
{
    l_neighbor = _left;
    r_neighbor = _right;
}

Philosopher::Philosopher(
                         Fork* _l_fork, 
                         Fork* _r_fork, 
                         int _min_think, 
                         int _max_think, 
                         int _min_eat, 
                         int _max_eat
                        )
                        : l_fork(_l_fork)
                        , r_fork(_r_fork)
                        , min_think(_min_think)
                        , max_think(_max_think)
                        , min_eat(_min_eat)
                        , max_eat(_max_eat) 
{
    curr_state     = THINKING;
    l_neighbor     = nullptr;
    r_neighbor     = nullptr;
    stop_requested = false;

    for (int i = 0; i < STATE_COUNT; i++) { state_times[i] = 0ms; }
}