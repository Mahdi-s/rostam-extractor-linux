#pragma once

#include <fstream>
#include <filesystem>
#include <iostream>

#include "rostam_utils.hpp"

enum class Mode { SEARCHING, HEADER, FILENAME, BODY };

class RostamStream {
public:
    explicit RostamStream(const std::filesystem::path& dir) : output_dir(dir) {}

    void feed(const uint8_t* data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            const uint8_t byte = data[i];

            switch (mode) {
                case Mode::SEARCHING:
                    magic_buffer.push_back(byte);
                    if (magic_buffer.size() > TsConstants::ROSTAM_MAGIC.size()) {
                        magic_buffer.erase(magic_buffer.begin());
                    }

                    if (magic_buffer == TsConstants::ROSTAM_MAGIC) {
                        header_buffer.clear();
                        filename_buffer.clear();
                        second_magic_resolved = false;
                        mode = Mode::HEADER;
                    }
                    break;

                case Mode::HEADER:
                    header_buffer.push_back(byte);

                    if (!second_magic_resolved) {
                        if (header_buffer.size() < TsConstants::ROSTAM_MAGIC.size()) {
                            break;
                        }

                        if (header_buffer == TsConstants::ROSTAM_MAGIC) {
                            header_buffer.clear();
                        }
                        second_magic_resolved = true;
                        break;
                    }

                    if (header_buffer.size() == TsConstants::HEADER_DATA_SIZE) {
                        expected_filename_len = decodeLittleEndian64(&header_buffer[2]);
                        expected_payload_size = decodeLittleEndian64(&header_buffer[10]);
                        current_written = 0;

                        if (expected_filename_len > 0 && expected_filename_len < 1024) {
                            filename_buffer.clear();
                            mode = Mode::FILENAME;
                        } else {
                            mode = Mode::SEARCHING;
                        }

                        header_buffer.clear();
                    }
                    break;

                case Mode::FILENAME:
                    filename_buffer.push_back(byte);

                    if (filename_buffer.size() == expected_filename_len) {
                        const std::string decoded_name = decodeFilename(filename_buffer);
                        std::cout << "# Extracting: " << decoded_name << "\n";
                        std::cout.flush();

                        current_file.open(output_dir / decoded_name, std::ios::binary);
                        current_written = 0;

                        if (expected_payload_size > 0) {
                            mode = Mode::BODY;
                        } else {
                            current_file.close();
                            mode = Mode::SEARCHING;
                        }

                        filename_buffer.clear();
                    }
                    break;

                case Mode::BODY:
                    current_file.put(static_cast<char>(byte));
                    ++current_written;

                    if (current_written >= expected_payload_size) {
                        current_file.close();
                        mode = Mode::SEARCHING;
                    }
                    break;
            }
        }
    }

private:
    Mode mode = Mode::SEARCHING;
    std::filesystem::path output_dir;
    std::ofstream current_file;
    std::vector<uint8_t> magic_buffer;
    std::vector<uint8_t> header_buffer;
    std::vector<uint8_t> filename_buffer;
    uint64_t expected_filename_len = 0;
    uint64_t expected_payload_size = 0;
    uint64_t current_written = 0;
    bool second_magic_resolved = false;
};
