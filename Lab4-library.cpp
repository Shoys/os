#include <SDL.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <queue>

#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <atomic>

#define max std::max
#define min std::min
#endif

#define THREADS 12
#define MAX_WORKERS 12
#define CONTRAST_FACTOR 128

void increaseContrast(SDL_Surface* image, int startY, int endY, Uint8 contrastFactor) {
    for (int y = startY; y < endY; ++y) {
        for (int x = 0; x < image->w; ++x) {
            Uint8* pixel = (Uint8*)image->pixels + y * image->pitch + x * image->format->BytesPerPixel;
            for (int c = 0; c < image->format->BytesPerPixel; ++c) {
                pixel[c] = max(0, pixel[c] - contrastFactor);
            }
        }
    }
}

#ifdef _WIN32
extern "C" __declspec(dllexport) int increaseContrast(std::string path) {
#else
extern "C" int increaseContrast(std::string path) {
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }

#ifdef _WIN32
    std::wstring wpath(path.begin(), path.end());
    HANDLE hFile = CreateFile(
        wpath.c_str(),
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Unable to open file" << std::endl;
        return 1;
    }

    HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMap == NULL) {
        std::cerr << "Could not create file mapping." << std::endl;
        CloseHandle(hFile);
        return 1;
    }

    char* pBuf = (char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (pBuf == NULL) {
        std::cerr << "Could not map view of file." << std::endl;
        CloseHandle(hMap);
        CloseHandle(hFile);
        return 1;
    }

    int file_size = GetFileSize(hFile, NULL);
    SDL_Surface* image = SDL_LoadBMP_RW(SDL_RWFromMem(pBuf, file_size), 1);


    UnmapViewOfFile(pBuf);
    CloseHandle(hMap);
    CloseHandle(hFile);
#else
    int fd;
    struct stat sb;
    char* file_data;

    fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Error: Unable to open file" << std::endl;
        return 1;
    }

    if (fstat(fd, &sb) == -1) {
        std::cerr << "Error: Unable to get file size" << std::endl;
        return 1;
    }

    file_data = static_cast<char*>(mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (file_data == MAP_FAILED) {
        std::cerr << "Error: Unable to map file" << std::endl;
        return 1;
    }
    SDL_Surface* image = SDL_LoadBMP_RW(SDL_RWFromMem(file_data, sb.st_size), 1);
    int file_size = sb.st_size;
    close(fd);
#endif

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
            pthread_setschedparam(pthread_self(), SCHED_BATCH, new sched_param{ sched_priority: 20 });
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
                increaseContrast(image, row, row + 1, CONTRAST_FACTOR);

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

    printf("Time taken: %lld microseconds\n", duration.count());

#ifdef _WIN32
    hFile = CreateFile(L"output.bmp", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Could not open file." << std::endl;
        return 1;
    }

    SetFilePointer(hFile, file_size, NULL, FILE_BEGIN);
    SetEndOfFile(hFile);
    file_size = GetFileSize(hFile, NULL);

    hMap = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (hMap == NULL) {
        std::cerr << "Could not create file mapping." << std::endl;
        CloseHandle(hFile);
        return 1;
    }

    pBuf = (char*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0);
    if (pBuf == NULL) {
        std::cerr << "Could not map view of file." << std::endl;
        CloseHandle(hMap);
        CloseHandle(hFile);
        return 1;
    }

    SDL_SaveBMP_RW(image, SDL_RWFromMem(pBuf, file_size), 1);

    UnmapViewOfFile(pBuf);
    CloseHandle(hMap);
    CloseHandle(hFile);
#else
    fd = open("output.bmp", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        std::cerr << "Error: Unable to open file" << std::endl;
        return 1;
    }

    ftruncate(fd, file_size);

    if (fstat(fd, &sb) == -1) {
        std::cerr << "Error: Unable to get file size" << std::endl;
        return 1;
    }

    file_data = static_cast<char*>(mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (file_data == MAP_FAILED) {
        std::cerr << "Error: Unable to map file" << std::endl;
        return 1;
    }
    SDL_SaveBMP_RW(image, SDL_RWFromMem(file_data, sb.st_size), 1);
    if (msync(file_data, sb.st_size, MS_SYNC) == -1) {
        std::cerr << "Error: Unable to sync file" << std::endl;
        return 1;
    }
    munmap(file_data, sb.st_size);
    close(fd);
#endif

    SDL_FreeSurface(image);
    SDL_Quit();
    return 0;
}

#ifdef _WIN32

extern "C" __declspec(dllexport) void CALLBACK contrast(HWND, HINSTANCE, LPSTR args, int, int i) {
    AllocConsole();
    FILE* in, * out, * err;
    freopen_s(&in, "CONIN$", "r", stdin);
    freopen_s(&out, "CONOUT$", "w", stdout);
    freopen_s(&err, "CONOUT$", "w", stderr);

    std::string path = args;
    increaseContrast(path);

    Sleep(10 * 1000); // Maybe not the best way to keep console window open, it's needed because rundll32.exe opens a program in a new console window that immediately closes
}
#endif
