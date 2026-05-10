#include "rostam_stream.hpp"

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [--pid PID] <input.ts> <output_dir>\n";
    std::cerr << "       " << program << " [-p PID] <input.ts> <output_dir>\n";
}

bool parse_pid_value(const std::string& text, uint16_t& pid) {
    if (text.empty()) {
        return false;
    }

    uint64_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }

        value = (value * 10) + static_cast<uint64_t>(ch - '0');
        if (value > 8191) {
            return false;
        }
    }

    pid = static_cast<uint16_t>(value);
    return true;
}

int skip_pes_and_ac3_headers(const uint8_t* packet, int offset) {
    if (offset + 8 >= TsConstants::TS_PACKET_SIZE) {
        return offset;
    }

    if (packet[offset] == TsConstants::PES_START_CODE[0] &&
        packet[offset + 1] == TsConstants::PES_START_CODE[1] &&
        packet[offset + 2] == TsConstants::PES_START_CODE[2] &&
        packet[offset + 3] == TsConstants::STREAM_ID_PRIVATE_1) {
        const uint8_t pes_len = packet[offset + 8];
        int total_skip = 9 + pes_len;

        if (offset + total_skip >= TsConstants::TS_PACKET_SIZE) {
            return TsConstants::TS_PACKET_SIZE;
        }

        if (offset + total_skip + 1 < TsConstants::TS_PACKET_SIZE &&
            packet[offset + total_skip] == 0x0B &&
            packet[offset + total_skip + 1] == 0x77) {
            total_skip += 7;
        }

        if (offset + total_skip >= TsConstants::TS_PACKET_SIZE) {
            return TsConstants::TS_PACKET_SIZE;
        }

        return offset + total_skip;
    }

    return offset;
}

int main(int argc, char** argv) {
    uint16_t target_pid = static_cast<uint16_t>(TsConstants::DEFAULT_PID);
    std::vector<std::string> positional_args;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        if (arg == "--pid" || arg == "-p") {
            if (i + 1 >= argc || !parse_pid_value(argv[i + 1], target_pid)) {
                std::cerr << "Invalid PID. Use a decimal value from 0 to 8191.\n";
                print_usage(argv[0]);
                return 1;
            }
            ++i;
        } else if (arg.rfind("--pid=", 0) == 0) {
            if (!parse_pid_value(arg.substr(6), target_pid)) {
                std::cerr << "Invalid PID. Use a decimal value from 0 to 8191.\n";
                print_usage(argv[0]);
                return 1;
            }
        } else {
            positional_args.push_back(arg);
        }
    }

    if (positional_args.size() != 2) {
        print_usage(argv[0]);
        return 1;
    }

    const std::filesystem::path input_path = positional_args[0];
    const std::filesystem::path output_dir = positional_args[1];

    std::filesystem::create_directories(output_dir);
    const uint64_t total_file_size = std::filesystem::file_size(input_path);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        std::cerr << "Could not open input file: " << input_path << "\n";
        return 1;
    }

    RostamStream stream(output_dir);
    uint64_t total_bytes_read = 0;
    uint64_t last_progress_bytes = 0;

    constexpr size_t CHUNK_PACKETS = 22310;
    constexpr size_t CHUNK_SIZE = CHUNK_PACKETS * TsConstants::TS_PACKET_SIZE;
    constexpr uint64_t PROGRESS_INTERVAL = 10ULL * 1024ULL * 1024ULL;
    std::vector<uint8_t> buffer(CHUNK_SIZE);

    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize bytes_read = input.gcount();
        if (bytes_read <= 0) {
            break;
        }

        total_bytes_read += static_cast<uint64_t>(bytes_read);

        const size_t processable_bytes =
            static_cast<size_t>(bytes_read) -
            (static_cast<size_t>(bytes_read) % TsConstants::TS_PACKET_SIZE);

        for (size_t pos = 0; pos < processable_bytes; pos += TsConstants::TS_PACKET_SIZE) {
            const uint8_t* packet = buffer.data() + pos;

            if (packet[0] != TsConstants::TS_SYNC_BYTE) {
                continue;
            }

            const uint16_t pid = static_cast<uint16_t>(((packet[1] & 0x1F) << 8) | packet[2]);
            if (pid != target_pid) {
                continue;
            }

            const uint8_t adapt_ctrl = (packet[3] >> 4) & 0x03;
            int offset = 0;

            if (adapt_ctrl == 1) {
                offset = 4;
            } else if (adapt_ctrl == 2) {
                continue;
            } else if (adapt_ctrl == 3) {
                offset = 5 + packet[4];
            } else {
                continue;
            }

            if (offset >= TsConstants::TS_PACKET_SIZE) {
                continue;
            }

            const bool pusi = (packet[1] & 0x40) != 0;
            if (pusi) {
                offset = skip_pes_and_ac3_headers(packet, offset);
            }

            if (offset < TsConstants::TS_PACKET_SIZE) {
                stream.feed(packet + offset, TsConstants::TS_PACKET_SIZE - offset);
            }
        }

        if (total_file_size > 0 && total_bytes_read - last_progress_bytes >= PROGRESS_INTERVAL) {
            const uint64_t percent = (total_bytes_read * 100) / total_file_size;
            std::cout << percent << "\n";
            std::cout.flush();
            last_progress_bytes = total_bytes_read;
        }
    }

    std::cout << "100\n";
    std::cout << "# Done!\n";
    std::cout.flush();

    return 0;
}
