// ReSharper disable CppClangTidyConcurrencyMtUnsafe
#include <iostream>
#include <Windows.h>

constexpr int DEFAULT_FILE_SIZE = 512 * 10;
constexpr size_t SUPPORTED_MAX_PATH = 512ULL;


int wmain(int argc, wchar_t* argv[]) {
	if (argc < 3) {
		std::cout << "Syntax: RandomFilesTester <FolderPath> <NumberOfFiles> [FileSize]" << std::endl;
		return 1;
	}

	const wchar_t* folderPath = argv[1];
	const size_t fileAmount = _wtoll(argv[2]);

	int fileSize = DEFAULT_FILE_SIZE;
	if (argc > 3) {
		fileSize = _wtoi(argv[3]);
	}

	wchar_t pathBuffer[SUPPORTED_MAX_PATH];
	byte* fileBytes = new byte[fileSize];

	// Always the same values
	srand(0);  // NOLINT(cert-msc51-cpp)
	for (int i = 0; i < fileSize; i++) {
		fileBytes[i] = (byte)rand();
	}

	for (size_t i = 0; i < fileAmount; i++) {
		swprintf_s(pathBuffer, L"%s\\Random%llu.bin", folderPath, i);

		const HANDLE file = CreateFileW(pathBuffer, SYNCHRONIZE | MAXIMUM_ALLOWED, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (!file || file == INVALID_HANDLE_VALUE) {
			std::cout << "Could not create file" << std::endl;
			return 1;
		}

		DWORD bytesWritten;
		WriteFile(file, fileBytes, fileSize, &bytesWritten, nullptr);

		CloseHandle(file);
	}

	std::cout << "Press any key to delete the generated files" << std::endl;
	system("pause");

	for (size_t i = 0; i < fileAmount; i++) {
		swprintf_s(pathBuffer, L"%s\\Random%llu.bin", folderPath, i);
		DeleteFileW(pathBuffer);
	}
}
