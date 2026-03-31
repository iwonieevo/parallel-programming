#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>
#include <omp.h>

long double calculate_euler_constant(size_t n, int threads)
{
    long double sum = 0.0L;
    #pragma omp parallel for num_threads(threads) reduction(+:sum)
    for (size_t k = 1; k <= n; k++)
    {
        sum += 1.0L / k;
    }
    return sum - std::log(static_cast<long double>(n));
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " N THREADS" << std::endl;
        return 1;
    }

    size_t n;
    int threads;
    try
    {
        n       = static_cast<size_t>(std::stoull(argv[1]));
        threads = std::stoi(argv[2]);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        return 1;
    }

    if (n == 0 || threads <= 0)
    {
        std::cerr << "N and THREADS must be positive integers." << std::endl;
        return 1;
    }

    int maxThreads = omp_get_max_threads();
    if (threads > maxThreads)
    {
        std::cerr << "Warning: requested " << threads
                  << " threads but max is " << maxThreads
                  << ". Clamping." << std::endl;
        threads = maxThreads;
    }

    std::cout << "Calculating Euler's constant (gamma) with n = " << n
              << " using " << threads << " threads..." << std::endl;

    double startTime = omp_get_wtime();
    long double euler_constant = calculate_euler_constant(n, threads);
    double elapsed = omp_get_wtime() - startTime;
    
    std::cout << "Calculated Euler's constant (gamma): " << std::setprecision(18) << euler_constant << std::endl
              << "Elapsed time: " << elapsed << " seconds" << std::endl;

    return 0;
}