#include <iostream>

extern "C" int increaseContrast(std::string path);

int main() {
	int result = increaseContrast("image.bmp");
	std::cout << "Statically linked result: " << result << std::endl;
}
