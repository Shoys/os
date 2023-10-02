#include <SDL.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#define max std::max
#define min std::min
#endif

#define METHOD 3 // 1 = No parallelism, 2 = Split rows into sectors, 3 = Every Xth row
#define THREADS 12

#define CONTRAST_FACTOR 128

void decreaseContrast(SDL_Surface* image, int startY, int endY, Uint8 contrastFactor) {
    for (int y = startY; y < endY; ++y) {
        for (int x = 0; x < image->w; ++x) {
            Uint8* pixel = (Uint8*)image->pixels + y * image->pitch + x * image->format->BytesPerPixel;
            for (int c = 0; c < image->format->BytesPerPixel; ++c) {
                pixel[c] = max(0, pixel[c] - contrastFactor);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Surface* image = SDL_LoadBMP("image.bmp");
    if (!image) {
        std::cerr << "Error: Unable to load image - " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    auto startTime = std::chrono::high_resolution_clock::now();

#if METHOD == 1
    #if _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
    pthread_setschedparam(pthread_self(), SCHED_BATCH, new sched_param { sched_priority: 20 });
#endif
    decreaseContrast(image, 0, image->h, CONTRAST_FACTOR);
# elif METHOD == 2
    // Create a vector to store thread objects
    std::vector<std::thread> threads;

    int everyXth = image->h / THREADS;

    // Start processing with multiple threads
    for (int i = 0; i < THREADS; ++i) {
        int startY = i * everyXth;
        int endY = (i == THREADS - 1) ? image->h : (i + 1) * everyXth;
        threads.emplace_back([startY, endY, &image] {
#if _WIN32
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
            pthread_setschedparam(pthread_self(), SCHED_BATCH, new sched_param { sched_priority: 20 });
#endif
            decreaseContrast(image, startY, endY, CONTRAST_FACTOR);
        });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }
# elif METHOD == 3
    std::vector<std::thread> threads;

    int everyXth = THREADS;

    for (int i = 0; i < THREADS; ++i) {
        int rows = image->h % everyXth > i ? image->h / everyXth + 1 : image->h / everyXth;
        threads.emplace_back([i, rows, everyXth, &image] {
        #if _WIN32
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        #else
            pthread_setschedparam(pthread_self(), SCHED_BATCH, new sched_param { sched_priority: 20 });
        #endif
            for (int j = 0; j < rows; j++) {
                int row = j * everyXth + i;
                decreaseContrast(image, row, row + 1, CONTRAST_FACTOR);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
#endif

    auto endTime = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    std::cout << "Time taken: " << duration.count() << " microseconds" << std::endl;

    SDL_SaveBMP(image, "output.bmp");

    SDL_FreeSurface(image);
    SDL_Quit();
    return 0;
}
