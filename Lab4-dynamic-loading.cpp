#include <iostream>
#ifdef _WIN32
#include <Windows.h>

typedef int (*IncreaseContrastFunction)(std::string path);
#else
#include <dlfcn.h>
#endif

int main() {
    #ifdef _WIN32
    HMODULE dllHandle = LoadLibrary(L"OSLab4.dll");
    if (dllHandle != NULL) {
        IncreaseContrastFunction increaseContrast = reinterpret_cast<IncreaseContrastFunction>(GetProcAddress(dllHandle, "increaseContrast"));
        if (increaseContrast != NULL) {
            int result = increaseContrast("image.bmp");
            std::cout << "Dynamically loaded result: " << result << std::endl;
        }
        FreeLibrary(dllHandle);
    } else {
        std::cerr << "Failed to load the library." << std::endl;
    }
    #else
    void* handle = dlopen("oslab4.so", RTLD_LAZY);
    if (!handle) {
        std::cerr << "Failed to load the library." << std::endl;
        exit(1);
    }

    int (*increaseContrast)(std::string) = (int (*)(std::string)) dlsym(handle, "increaseContrast");
    char* err;
    if ((err = dlerror()) != NULL)  {
        std::cerr << "Failed to load the library." << std::endl << err << std::endl;
        exit(1);
    }

    std::cout << "Dynamically loaded result: " << (*increaseContrast)("image.bmp") << std::endl;
    dlclose(handle);
    #endif
    return 0;
}
