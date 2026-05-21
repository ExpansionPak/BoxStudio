#include <fstream>
#include "Rom.h"

bool Rom::load(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    // Read the entire file into the data vector
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    data.resize(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    // Parse the ROM header
    if (fileSize < sizeof(RomHeader))
        return false;

    header = *reinterpret_cast<RomHeader*>(data.data());

    filepath = path;
    loaded = true;
    return true;
}

uint8_t Rom::read_u8(uint32_t offset) const
{
    // your code here
}