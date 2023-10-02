#include <SDL.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <queue>
#include <time.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#define max std::max
#define min std::min
#endif

#define THREADS 1
#define MAX_WORKERS 12
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

    std::queue<int> rowsToProcess;
    for (int i = 0; i < image->h; i++) {
        rowsToProcess.push(i);
    }
    int currentlyWorking = 0;
#ifdef _WIN32
    HANDLE queueMutex = CreateMutex(
        NULL,
        FALSE,
        NULL);
#else
    pthread_mutex_t queueMutex;
    if (pthread_mutex_init(&queueMutex, NULL) != 0) {
        std::cerr << "Mutex init has failed" << std::endl;
        return 1;
    }
#endif

    std::vector<std::thread> threads;

    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back([&rowsToProcess, &image, &queueMutex, &currentlyWorking] {
#if _WIN32
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
            pthread_setschedparam(pthread_self(), SCHED_BATCH, new sched_param { sched_priority: 20 });
#endif
            while (true) {
#ifdef _WIN32
                WaitForSingleObject(queueMutex, INFINITE);
#else
                pthread_mutex_lock(&queueMutex);
#endif
                bool empty = rowsToProcess.empty();
                int row = -1;
                if (!empty && currentlyWorking < MAX_WORKERS) {
                    currentlyWorking++;
                    row = rowsToProcess.front();
                    rowsToProcess.pop();
                }
#ifdef _WIN32
                ReleaseMutex(queueMutex);
#else
                pthread_mutex_unlock(&queueMutex);
#endif

                if (empty) {
                    break;
                }
                if (row == -1) {
                    continue;
                }
                decreaseContrast(image, row, row + 1, CONTRAST_FACTOR);

#ifdef _WIN32
                WaitForSingleObject(queueMutex, INFINITE);
#else
                pthread_mutex_lock(&queueMutex);
#endif
                currentlyWorking--;
#ifdef _WIN32
                ReleaseMutex(queueMutex);
#else
                pthread_mutex_unlock(&queueMutex);
#endif
            }
        });
    }

    std::atomic_bool done;
    std::thread progressBar([&done, &rowsToProcess, &queueMutex, &image] {
        while (true) {
#ifdef _WIN32
            WaitForSingleObject(queueMutex, INFINITE);
#else
            pthread_mutex_lock(&queueMutex);
#endif
            int left = rowsToProcess.size();
#ifdef _WIN32
            ReleaseMutex(queueMutex);
#else
            pthread_mutex_unlock(&queueMutex);
#endif
            printf("\u001b[2K\u001b[0G%.2f%%", 100 * (1 - (double)left / image->h));
            if (done.load()) {
                std::cout << "\u001b[2K\u001b[0G";
                break;
            }
#ifdef _WIN32
            Sleep(1); // 1ms
#else
            usleep(1000); // 1ms
#endif
        }
    });

    for (auto& thread : threads) {
        thread.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();

    done.store(true);
    progressBar.join();

#ifdef _WIN32
    CloseHandle(queueMutex);
#else
    pthread_mutex_destroy(&queueMutex);
#endif

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    printf("Time taken: %d microseconds\n", duration.count());

    SDL_SaveBMP(image, "output.bmp");

    SDL_FreeSurface(image);
    SDL_Quit();
    return 0;
}
