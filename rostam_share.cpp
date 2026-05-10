#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int DEFAULT_PORT = 8080;
constexpr int QR_VERSION = 3;
constexpr int QR_SIZE = 17 + (4 * QR_VERSION);
constexpr int QR_DATA_CODEWORDS = 55;
constexpr int QR_ECC_CODEWORDS = 15;

volatile std::sig_atomic_t keep_running = 1;

void handle_signal(int) {
    keep_running = 0;
}

bool send_all(int fd, const char* data, size_t length) {
    while (length > 0) {
        const ssize_t sent = send(fd, data, length, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        length -= static_cast<size_t>(sent);
    }
    return true;
}

bool send_string(int fd, const std::string& text) {
    return send_all(fd, text.data(), text.size());
}

std::string html_escape(const std::string& text) {
    std::string out;
    for (char ch : text) {
        if (ch == '&') {
            out += "&amp;";
        } else if (ch == '<') {
            out += "&lt;";
        } else if (ch == '>') {
            out += "&gt;";
        } else if (ch == '"') {
            out += "&quot;";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string url_encode(const std::string& text) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : text) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                          ch == '.' || ch == '~' || ch == '/';
        if (safe) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0F]);
        }
    }
    return out;
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    return -1;
}

std::string url_decode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex_value(text[i + 1]);
            const int lo = hex_value(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(text[i]);
            }
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

std::string mime_type(const std::filesystem::path& path) {
    const std::string ext = lower_extension(path);
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".txt" || ext == ".log") return "text/plain; charset=utf-8";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".ts") return "video/mp2t";
    return "application/octet-stream";
}

std::string format_size(uintmax_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream out;
    if (unit == 0) {
        out << bytes << " " << units[unit];
    } else {
        out.setf(std::ios::fixed);
        out.precision(value >= 10.0 ? 1 : 2);
        out << value << " " << units[unit];
    }
    return out.str();
}

bool has_parent_component(const std::filesystem::path& path) {
    for (const auto& part : path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

void send_response(int fd, int status, const std::string& status_text,
                   const std::string& content_type, const std::string& body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << " " << status_text << "\r\n";
    header << "Content-Type: " << content_type << "\r\n";
    header << "Content-Length: " << body.size() << "\r\n";
    header << "Connection: close\r\n\r\n";
    send_string(fd, header.str());
    send_string(fd, body);
}

void send_error_page(int fd, int status, const std::string& status_text) {
    const std::string body = "<!doctype html><html><head><meta charset=\"utf-8\">"
                             "<title>" + status_text + "</title></head><body><h1>" +
                             status_text + "</h1></body></html>";
    send_response(fd, status, status_text, "text/html; charset=utf-8", body);
}

std::string build_directory_page(const std::filesystem::path& root,
                                 const std::filesystem::path& relative) {
    const std::filesystem::path directory = root / relative;
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory()) {
            return a.is_directory();
        }
        return a.path().filename().string() < b.path().filename().string();
    });

    std::ostringstream body;
    body << "<!doctype html><html><head><meta charset=\"utf-8\">";
    body << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    body << "<title>Rostam Files</title>";
    body << "<style>";
    body << "body{font-family:sans-serif;margin:24px;max-width:900px}";
    body << "h1{font-size:24px}a{display:block;padding:12px;border-bottom:1px solid #ddd;color:#0645ad;text-decoration:none}";
    body << ".size{float:right;color:#666}.hint{color:#555;margin-bottom:18px}";
    body << "</style></head><body>";
    body << "<h1>Rostam Files</h1>";
    body << "<div class=\"hint\">Tap a file to download it. Keep this computer awake while downloading.</div>";

    if (!relative.empty()) {
        std::filesystem::path parent = relative.parent_path();
        body << "<a href=\"/" << url_encode(parent.generic_string()) << "\">../</a>";
    }

    for (const auto& entry : entries) {
        const std::string name = entry.path().filename().string();
        std::filesystem::path link_path = relative / name;
        std::string href = "/" + url_encode(link_path.generic_string());
        if (entry.is_directory()) {
            href += "/";
        }

        body << "<a href=\"" << href << "\"";
        if (!entry.is_directory()) {
            body << " download";
        }
        body << ">";
        body << html_escape(name);
        if (entry.is_directory()) {
            body << "/";
        } else {
            body << "<span class=\"size\">" << format_size(entry.file_size()) << "</span>";
        }
        body << "</a>";
    }

    body << "</body></html>";
    return body.str();
}

void serve_file(int fd, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        send_error_page(fd, 404, "Not Found");
        return;
    }

    const uintmax_t file_size = std::filesystem::file_size(path);
    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\n";
    header << "Content-Type: " << mime_type(path) << "\r\n";
    header << "Content-Length: " << file_size << "\r\n";
    header << "Connection: close\r\n\r\n";
    if (!send_string(fd, header.str())) {
        return;
    }

    std::vector<char> buffer(64 * 1024);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = file.gcount();
        if (got > 0 && !send_all(fd, buffer.data(), static_cast<size_t>(got))) {
            return;
        }
    }
}

void handle_client(int client_fd, const std::filesystem::path& root) {
    std::string request;
    char buffer[1024];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
        const ssize_t got = recv(client_fd, buffer, sizeof(buffer), 0);
        if (got <= 0) {
            return;
        }
        request.append(buffer, static_cast<size_t>(got));
    }

    const size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        send_error_page(client_fd, 400, "Bad Request");
        return;
    }

    std::istringstream first_line(request.substr(0, line_end));
    std::string method;
    std::string raw_path;
    std::string version;
    first_line >> method >> raw_path >> version;

    if (method != "GET" && method != "HEAD") {
        send_error_page(client_fd, 405, "Method Not Allowed");
        return;
    }

    const size_t query_pos = raw_path.find('?');
    if (query_pos != std::string::npos) {
        raw_path.resize(query_pos);
    }

    std::string decoded = url_decode(raw_path);
    while (!decoded.empty() && decoded.front() == '/') {
        decoded.erase(decoded.begin());
    }

    const std::filesystem::path relative = std::filesystem::path(decoded).lexically_normal();
    if (relative.is_absolute() || has_parent_component(relative)) {
        send_error_page(client_fd, 403, "Forbidden");
        return;
    }

    const std::filesystem::path target = root / relative;
    if (!std::filesystem::exists(target)) {
        send_error_page(client_fd, 404, "Not Found");
        return;
    }

    if (std::filesystem::is_directory(target)) {
        send_response(client_fd, 200, "OK", "text/html; charset=utf-8",
                      build_directory_page(root, relative));
    } else if (std::filesystem::is_regular_file(target)) {
        serve_file(client_fd, target);
    } else {
        send_error_page(client_fd, 403, "Forbidden");
    }
}

uint8_t gf_multiply(uint8_t x, uint8_t y) {
    int product = 0;
    for (int i = 7; i >= 0; --i) {
        product = (product << 1) ^ (((product >> 7) & 1) * 0x11D);
        product ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(product);
}

std::vector<uint8_t> reed_solomon_generator(int degree) {
    std::vector<uint8_t> result(static_cast<size_t>(degree));
    result[static_cast<size_t>(degree - 1)] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[static_cast<size_t>(j)] = gf_multiply(result[static_cast<size_t>(j)], root);
            if (j + 1 < degree) {
                result[static_cast<size_t>(j)] ^= result[static_cast<size_t>(j + 1)];
            }
        }
        root = gf_multiply(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> reed_solomon_remainder(const std::vector<uint8_t>& data) {
    const std::vector<uint8_t> generator = reed_solomon_generator(QR_ECC_CODEWORDS);
    std::vector<uint8_t> result(QR_ECC_CODEWORDS);

    for (uint8_t byte : data) {
        const uint8_t factor = byte ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < generator.size(); ++i) {
            result[i] ^= gf_multiply(generator[i], factor);
        }
    }

    return result;
}

void append_bits(std::vector<bool>& bits, uint32_t value, int count) {
    for (int i = count - 1; i >= 0; --i) {
        bits.push_back(((value >> i) & 1) != 0);
    }
}

bool qr_mask(int x, int y) {
    return ((x + y) % 2) == 0;
}

void set_module(std::vector<std::vector<int>>& qr, std::vector<std::vector<bool>>& reserved,
                int x, int y, bool dark) {
    if (x >= 0 && y >= 0 && x < QR_SIZE && y < QR_SIZE) {
        qr[static_cast<size_t>(y)][static_cast<size_t>(x)] = dark ? 1 : 0;
        reserved[static_cast<size_t>(y)][static_cast<size_t>(x)] = true;
    }
}

void draw_finder(std::vector<std::vector<int>>& qr, std::vector<std::vector<bool>>& reserved,
                 int left, int top) {
    for (int y = -1; y <= 7; ++y) {
        for (int x = -1; x <= 7; ++x) {
            const int xx = left + x;
            const int yy = top + y;
            if (xx < 0 || yy < 0 || xx >= QR_SIZE || yy >= QR_SIZE) {
                continue;
            }

            const bool in_pattern = x >= 0 && x <= 6 && y >= 0 && y <= 6;
            bool dark = false;
            if (in_pattern) {
                dark = x == 0 || x == 6 || y == 0 || y == 6 ||
                       (x >= 2 && x <= 4 && y >= 2 && y <= 4);
            }
            set_module(qr, reserved, xx, yy, dark);
        }
    }
}

void draw_alignment(std::vector<std::vector<int>>& qr, std::vector<std::vector<bool>>& reserved,
                    int center_x, int center_y) {
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const bool dark = std::max(std::abs(x), std::abs(y)) != 1;
            set_module(qr, reserved, center_x + x, center_y + y, dark);
        }
    }
}

bool get_bit(int value, int index) {
    return ((value >> index) & 1) != 0;
}

void draw_format_bits(std::vector<std::vector<int>>& qr, std::vector<std::vector<bool>>& reserved) {
    const int error_correction_low = 1;
    const int mask = 0;
    const int data = (error_correction_low << 3) | mask;
    int remainder = data;
    for (int i = 0; i < 10; ++i) {
        remainder = (remainder << 1) ^ (((remainder >> 9) & 1) * 0x537);
    }
    const int bits = ((data << 10) | (remainder & 0x3FF)) ^ 0x5412;

    for (int i = 0; i <= 5; ++i) set_module(qr, reserved, 8, i, get_bit(bits, i));
    set_module(qr, reserved, 8, 7, get_bit(bits, 6));
    set_module(qr, reserved, 8, 8, get_bit(bits, 7));
    set_module(qr, reserved, 7, 8, get_bit(bits, 8));
    for (int i = 9; i < 15; ++i) set_module(qr, reserved, 14 - i, 8, get_bit(bits, i));

    for (int i = 0; i < 8; ++i) set_module(qr, reserved, QR_SIZE - 1 - i, 8, get_bit(bits, i));
    for (int i = 8; i < 15; ++i) set_module(qr, reserved, 8, QR_SIZE - 15 + i, get_bit(bits, i));
    set_module(qr, reserved, 8, QR_SIZE - 8, true);
}

bool make_qr(const std::string& text, std::vector<std::vector<int>>& qr) {
    if (text.size() > 53) {
        return false;
    }

    std::vector<bool> bits;
    append_bits(bits, 0x4, 4);
    append_bits(bits, static_cast<uint32_t>(text.size()), 8);
    for (unsigned char ch : text) {
        append_bits(bits, ch, 8);
    }

    const size_t capacity_bits = QR_DATA_CODEWORDS * 8;
    const size_t terminator = std::min<size_t>(4, capacity_bits - bits.size());
    for (size_t i = 0; i < terminator; ++i) {
        bits.push_back(false);
    }
    while (bits.size() % 8 != 0) {
        bits.push_back(false);
    }

    std::vector<uint8_t> data;
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; ++j) {
            byte = static_cast<uint8_t>((byte << 1) | (bits[i + static_cast<size_t>(j)] ? 1 : 0));
        }
        data.push_back(byte);
    }

    for (uint8_t pad = 0xEC; data.size() < QR_DATA_CODEWORDS; pad = (pad == 0xEC ? 0x11 : 0xEC)) {
        data.push_back(pad);
    }

    std::vector<uint8_t> codewords = data;
    const std::vector<uint8_t> ecc = reed_solomon_remainder(data);
    codewords.insert(codewords.end(), ecc.begin(), ecc.end());

    qr.assign(QR_SIZE, std::vector<int>(QR_SIZE, -1));
    std::vector<std::vector<bool>> reserved(QR_SIZE, std::vector<bool>(QR_SIZE, false));

    draw_finder(qr, reserved, 0, 0);
    draw_finder(qr, reserved, QR_SIZE - 7, 0);
    draw_finder(qr, reserved, 0, QR_SIZE - 7);
    draw_alignment(qr, reserved, 22, 22);

    for (int i = 8; i < QR_SIZE - 8; ++i) {
        set_module(qr, reserved, 6, i, (i % 2) == 0);
        set_module(qr, reserved, i, 6, (i % 2) == 0);
    }

    draw_format_bits(qr, reserved);

    std::vector<bool> code_bits;
    for (uint8_t byte : codewords) {
        append_bits(code_bits, byte, 8);
    }

    size_t bit_index = 0;
    bool upward = true;
    for (int right = QR_SIZE - 1; right >= 1; right -= 2) {
        if (right == 6) {
            --right;
        }

        for (int vert = 0; vert < QR_SIZE; ++vert) {
            const int y = upward ? (QR_SIZE - 1 - vert) : vert;
            for (int dx = 0; dx < 2; ++dx) {
                const int x = right - dx;
                if (reserved[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                    continue;
                }

                bool dark = false;
                if (bit_index < code_bits.size()) {
                    dark = code_bits[bit_index++];
                }
                if (qr_mask(x, y)) {
                    dark = !dark;
                }
                qr[static_cast<size_t>(y)][static_cast<size_t>(x)] = dark ? 1 : 0;
            }
        }
        upward = !upward;
    }

    return true;
}

void print_qr(const std::string& text) {
    std::vector<std::vector<int>> qr;
    if (!make_qr(text, qr)) {
        std::cout << "QR code skipped: URL is too long for the built-in QR encoder.\n";
        return;
    }

    constexpr int border = 4;
    for (int y = -border; y < QR_SIZE + border; ++y) {
        for (int x = -border; x < QR_SIZE + border; ++x) {
            const bool dark = x >= 0 && y >= 0 && x < QR_SIZE && y < QR_SIZE &&
                              qr[static_cast<size_t>(y)][static_cast<size_t>(x)] == 1;
            std::cout << (dark ? "\033[40m  \033[0m" : "\033[47m  \033[0m");
        }
        std::cout << "\n";
    }
}

std::vector<std::string> get_lan_ips() {
    std::vector<std::string> ips;
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return ips;
    }

    for (ifaddrs* item = interfaces; item != nullptr; item = item->ifa_next) {
        if (item->ifa_addr == nullptr || item->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((item->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        char address[INET_ADDRSTRLEN] = {};
        const auto* addr = reinterpret_cast<sockaddr_in*>(item->ifa_addr);
        if (inet_ntop(AF_INET, &addr->sin_addr, address, sizeof(address)) != nullptr) {
            ips.push_back(address);
        }
    }

    freeifaddrs(interfaces);
    std::sort(ips.begin(), ips.end());
    ips.erase(std::unique(ips.begin(), ips.end()), ips.end());
    return ips;
}

bool parse_port(const std::string& text, int& port) {
    if (text.empty()) {
        return false;
    }

    int value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = (value * 10) + (ch - '0');
        if (value > 65535) {
            return false;
        }
    }

    if (value <= 0) {
        return false;
    }
    port = value;
    return true;
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <folder> [--port PORT]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path root;
    int port = DEFAULT_PORT;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        if (arg == "--port" || arg == "-p") {
            if (i + 1 >= argc || !parse_port(argv[i + 1], port)) {
                std::cerr << "Invalid port. Use a value from 1 to 65535.\n";
                print_usage(argv[0]);
                return 1;
            }
            ++i;
        } else if (arg.rfind("--port=", 0) == 0) {
            if (!parse_port(arg.substr(7), port)) {
                std::cerr << "Invalid port. Use a value from 1 to 65535.\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (root.empty()) {
            root = arg;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (root.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        std::cerr << "Folder does not exist: " << root << "\n";
        return 1;
    }

    root = std::filesystem::canonical(root);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Could not create server socket: " << std::strerror(errno) << "\n";
        return 1;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::cerr << "Could not bind to port " << port << ": " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) != 0) {
        std::cerr << "Could not listen on port " << port << ": " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    const std::vector<std::string> ips = get_lan_ips();
    std::string primary_url = "http://127.0.0.1:" + std::to_string(port) + "/";
    if (!ips.empty()) {
        primary_url = "http://" + ips[0] + ":" + std::to_string(port) + "/";
    }

    std::cout << "\nRostam Wi-Fi Share\n";
    std::cout << "Serving folder: " << root << "\n\n";
    if (!ips.empty()) {
        std::cout << "Scan this QR code from a phone on the same Wi-Fi:\n\n";
        print_qr(primary_url);
        std::cout << "\nOpen this address on another device:\n";
        for (const std::string& ip : ips) {
            std::cout << "  http://" << ip << ":" << port << "/\n";
        }
    } else {
        std::cout << "No non-loopback network address was found.\n";
        std::cout << "Local test address: " << primary_url << "\n";
    }
    std::cout << "\nPress Ctrl+C to stop sharing.\n\n";

    while (keep_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        timeval timeout = {};
        timeout.tv_sec = 1;

        const int ready = select(server_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Server wait failed: " << std::strerror(errno) << "\n";
            break;
        }
        if (ready == 0) {
            continue;
        }

        sockaddr_in client_addr = {};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Accept failed: " << std::strerror(errno) << "\n";
            break;
        }

        handle_client(client_fd, root);
        close(client_fd);
    }

    close(server_fd);
    std::cout << "\nSharing stopped.\n";
    return 0;
}
