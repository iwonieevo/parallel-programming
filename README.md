# parallel-programming
Programs created as part of the **Parallel and distributed programming** class @ WUST

## Dining philosophers
A concurrent simulation of the Dining Philosophers problem using threads and mutexes, based on the [Chandy-Misra solution](https://en.wikipedia.org/wiki/Dining_philosophers_problem#Chandy/Misra_solution).

### Requirements
- A C++ compiler (e.g. `g++`)
- Terminal UI library:
  - Linux: [ncurses](https://man7.org/linux/man-pages/man3/ncurses.3x.html)
  - Windows: [PDCurses](https://pdcurses.org/)

### Compilation
#### Linux
```sh
g++ dining_philosophers.cpp -o dining_philosophers -lncurses -pthread
```

#### Windows (MinGW + PDCurses)
```pwsh
g++ -I"path\to\PDCurses" dining_philosophers.cpp -L"path\to\PDCurses\wincon" -l:pdcurses.a -o dining_philosophers.exe
```

### Usage
```sh
./dining_philosophers <N> <minThink> <maxThink> <minEat> <maxEat>
```
- **N** - number of philosophers
- **minThink** - minimum thinking time of each philosopher in milliseconds [ms]
- **maxThink** - maximum thinking time of each philosopher in milliseconds [ms]
- **minEat** - minimum eating time of each philosopher in milliseconds [ms]
- **maxEat** - maximum eating time of each philosopher in milliseconds [ms]

Example:
```sh
./dining_philosophers 5 100 500 100 500
```