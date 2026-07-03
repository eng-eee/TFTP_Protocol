/**
 * @file producer.cpp
 * @brief Simple TFTP-style UDP server with request handling and transfer sockets.
 *
 * This implementation parses incoming TFTP requests and dispatches read/write
 * operations to transfer sockets using a thread pool.
 */

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <vector>
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>

#define TFTP_PORT    69
#define TFTP_MAX_DATA_SIZE 512
#define TFTP_MAX_DATA_PACKET_SIZE (TFTP_MAX_DATA_SIZE + 4) // 512 bytes data + block header(2 byte opcode + 2 byte block number)
#define MAX_BUFFER_SIZE 1024
#define DEBUG
#define TIMEOUT_1Sec 1
#define NUMBER_OF_WORKERS 5
#define TRANSFER_RECV_TIMEOUT_SEC 5
#define MAX_RECV_RETRIES 5

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("TFTPServer"));
static volatile std::sig_atomic_t g_stop_flag = 0; // Signal-safe shutdown flag

/**
 * @enum OpCode
 * @brief TFTP packet operation codes.
 *
 * The opcode is stored in the first two bytes of a TFTP packet.
 */
enum OpCode : uint16_t {
    UNKNOWN_OP = 0U, // Explicit "not parsed" value
    RRQ = 1U, // Read Request
    WRQ = 2U, // Write Request
    DATA = 3U, // Data Packet
    ACK = 4U, // Acknowledgment
    ERROR = 5U // Error Packet
};

/**
 * @enum Mode
 * @brief TFTP transfer modes supported by the server.
 *
 * These values represent the negotiated transfer mode from the request.
 */
enum Mode : uint16_t {
    NETASCII = 1U, // Network ASCII
    OCTET = 2U, // Binary
    MAIL = 3U // Mail
};

/**
 * @enum ResultStatus
 * @brief Result codes used throughout the TFTP server.
 */
enum ResultStatus : int {
    ResultSuccess = 0,
    SocketCreateFailed = 1,
    BindFailed = 2,
    ListenFailed = 3,
    AcceptFailed = 4,
    SendFailed = 5,
    RecvFailed = 6,
    FileNotFound = 7,
    FileWriteError = 9,
    FileReadError = 8,
    InvalidRequest = 10,
    UnknownError = 11
};

/**
 * @struct TFTPRequest
 * @brief Parsed TFTP request data.
 *
 * Contains the request opcode, filename, and requested transfer mode.
 */
struct TFTPRequest {
    OpCode op_code = UNKNOWN_OP;
    std::string filename;
    Mode mode = OCTET;
};

/**
 * @brief Send a TFTP error packet to the client.
 */
static void send_error(int transfer_fd, sockaddr_in client_addr, uint16_t error_code, const std::string& error_msg) {
    size_t msg_length = error_msg.length();
    std::vector<char> error_packet(6 + msg_length); // 6 bytes for header + error message length
    error_packet[0] = 0; // Opcode high byte
    error_packet[1] = 5; // ERROR opcode low byte
    error_packet[2] = (error_code >> 8) & 0xFF; // Error code high byte
    error_packet[3] = error_code & 0xFF; // Error code low byte
    std::memcpy(&error_packet[4], error_msg.c_str(), msg_length);
    error_packet[4 + msg_length] = '\0'; // Null-terminate the error message
    ssize_t sent = sendto(transfer_fd, error_packet.data(), error_packet.size(), 0,
                           (sockaddr*)&client_addr, sizeof(client_addr));
    if (sent < 0) {
        perror("sendto (error)");
    }
    logger->info("Send ERROR with error code: " + std::to_string(error_code) + ", message: " + error_msg);
}

/**
 * @brief Send a TFTP ACK packet to the client.
 */
static void send_ack(int transfer_fd, sockaddr_in client_addr, uint16_t block_number) {
    char ack_packet[4];
    ack_packet[0] = 0; // Opcode high byte
    ack_packet[1] = 4; // ACK opcode low byte
    ack_packet[2] = (block_number >> 8) & 0xFF; // Block number high byte
    ack_packet[3] = block_number & 0xFF; // Block number low byte
    ssize_t sent = sendto(transfer_fd, ack_packet, sizeof(ack_packet), 0, (sockaddr*)&client_addr, sizeof(client_addr));
    if (sent < 0) {
        perror("sendto (ack)");
    }
    logger->info("Send ACK with block number: " + std::to_string(block_number));
}

/**
 * @brief Compare two sockaddr_in for equal address+port.
 */
static bool same_endpoint(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

/**
 * @brief Apply a receive timeout to a socket so blocking calls can't hang forever.
 */
static void set_recv_timeout(int socket_fd, int timeout_sec) {
    timeval tv{};
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt(SO_RCVTIMEO)");
    }
}

/**
 * @brief Parse an incoming RRQ or WRQ TFTP packet.
 */
TFTPRequest parse_rrq_packet(const char* rrq_packet, size_t packet_size) {
    TFTPRequest request;
    // Extract the opcode from the packet
    if (packet_size < 2) {
        request.op_code = UNKNOWN_OP; // Invalid
        return request;
    }

    uint16_t opcode = (static_cast<uint8_t>(rrq_packet[0]) << 8) | static_cast<uint8_t>(rrq_packet[1]);
    request.op_code = static_cast<OpCode>(opcode);

    // Extract the filename from the packet
    size_t index = 2; // Start after the opcode
    while (index < packet_size && rrq_packet[index] != '\0') {
        request.filename += rrq_packet[index++];
    }

    if (index >= packet_size) {
        logger->info("Malformed request: missing filename terminator");
        return request;
    }
    index++; // Skip the null terminator

    // Extract the mode from the packet (default already set to OCTET above)
    if (index < packet_size) {
        switch (rrq_packet[index]) {
            case 'n':
            case 'N':
                request.mode = NETASCII;
                break;
            case 'o':
            case 'O':
                request.mode = OCTET;
                break;
            case 'm':
            case 'M':
                request.mode = MAIL;
                break;
            default:
                request.mode = OCTET; // Default to binary mode
                break;
        }
    }
    std::stringstream ss;
    ss << "Parsed request - Opcode: " << std::to_string(request.op_code)
       << ", Filename: " << request.filename
       << ", Mode: " << std::to_string(request.mode);
    logger->info(ss.str());
    return request;
}

/**
 * @class TFTPRequestHandler
 * @brief Base class for TFTP request handling tasks.
 *
 * Derived handlers implement read and write transfer logic.
 */
class TFTPRequestHandler {
public:
    TFTPRequestHandler(sockaddr_in client_addr) : m_client_addr(client_addr) {}
    virtual ~TFTPRequestHandler() = default;
    virtual void handle_request() = 0;
    virtual void stop() = 0;
protected:
    int initialize_transfer_socket() {
        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            perror("socket");
            return -1;
        }
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(0); // Let the OS choose an available port

        if (bind(socket_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("bind");
            close(socket_fd);
            return -1;
        }

        set_recv_timeout(socket_fd, TRANSFER_RECV_TIMEOUT_SEC);

        socklen_t len = sizeof(server_addr);
        if (getsockname(socket_fd, (sockaddr*)&server_addr, &len) == 0) {
            logger->info("Transfer socket initialized on port: " + std::to_string(ntohs(server_addr.sin_port)) + " successfully.");
        }
        return socket_fd;
    }
    sockaddr_in m_client_addr;
};

/**
 * @class TFTPReadRequestHandler
 * @brief Handles TFTP RRQ read requests.
 */
class TFTPReadRequestHandler : public TFTPRequestHandler {
public:
    TFTPReadRequestHandler(sockaddr_in client_addr, TFTPRequest request) : TFTPRequestHandler(client_addr), m_request(request) {}
    void handle_request() override {
        // Handle read request (RRQ)
#ifdef DEBUG
        std::stringstream ss;
        ss << "Handling read-request from client: "
           << inet_ntoa(m_client_addr.sin_addr)
           << ":" << ntohs(m_client_addr.sin_port);
        logger->info(ss.str());
#endif
        int transfer_fd = initialize_transfer_socket();
        if (transfer_fd < 0) {
            logger->info("Failed to initialize transfer socket");
            return;
        }

        // Check file existence and read the file from the file system
        std::ifstream file(m_request.filename, std::ios::binary);
        if (!file.is_open()) {
            send_error(transfer_fd, m_client_addr, 1, "File not found");
            close(transfer_fd);
            return;
        }

        // Read 512 bytes of data from the file and send it to the client in a loop
        while (file && !m_stop) {
            char data_packet[TFTP_MAX_DATA_PACKET_SIZE];
            data_packet[0] = 0; // Opcode high byte
            data_packet[1] = 3; // DATA opcode low byte
            data_packet[2] = (m_block_number >> 8) & 0xFF; // Block number high byte
            data_packet[3] = m_block_number & 0xFF; // Block number low byte

            file.read(&data_packet[4], TFTP_MAX_DATA_SIZE);
            std::streamsize bytes_read = file.gcount();

            ssize_t sent_bytes = sendto(transfer_fd, data_packet, bytes_read + 4, 0,
                                         (sockaddr*)&m_client_addr, sizeof(m_client_addr));
            if (sent_bytes < 0) {
                perror("sendto");
                break;
            }
            logger->info("Sent data packet with block number: " + std::to_string(m_block_number) + ", bytes sent: " + std::to_string(sent_bytes));

            // Wait for ACK from the client for each data packet sent.
            bool got_valid_ack = false;
            for (int attempt = 0; attempt < MAX_RECV_RETRIES && !m_stop; ++attempt) {
                char ack_buffer[4];
                sockaddr_in from_addr{};
                socklen_t from_len = sizeof(from_addr);
                ssize_t n = recvfrom(transfer_fd, ack_buffer, sizeof(ack_buffer), 0,
                                      (sockaddr*)&from_addr, &from_len);
                if (n < 0) {
                    logger->info("Timed out waiting for ACK (attempt " + std::to_string(attempt + 1) + ")");
                    continue;
                }
                if (n != 4) {
                    logger->info("Invalid ACK size: " + std::to_string(n));
                    continue;
                }
                if (!same_endpoint(from_addr, m_client_addr)) {
                    logger->info("Ignoring packet from unexpected sender");
                    continue;
                }
                uint16_t received_block_number = (static_cast<uint8_t>(ack_buffer[2]) << 8) | static_cast<uint8_t>(ack_buffer[3]);
                if (received_block_number != m_block_number) {
                    logger->info("Unexpected block number in ACK: " + std::to_string(received_block_number));
                    continue;
                }
                logger->info("Received ACK with block number: " + std::to_string(m_block_number));
                got_valid_ack = true;
                break;
            }

            if (!got_valid_ack) {
                logger->info("Giving up on transfer after repeated missing/invalid ACKs");
                break;
            }

            // If the number of bytes read is less than 512, this was the final block.
            if (bytes_read < TFTP_MAX_DATA_SIZE) {
#ifdef DEBUG
                logger->info("End of file reached, transfer complete.");
#endif
                break;
            }
            m_block_number++;
        }
        close(transfer_fd);
    }
    void stop() override {
        m_stop = true;
    }
private:
    std::atomic<bool> m_stop{false};
    uint16_t m_block_number{1};
    TFTPRequest m_request;
};

/**
 * @class TFTPWriteRequestHandler
 * @brief Handles TFTP WRQ write requests.
 */
class TFTPWriteRequestHandler : public TFTPRequestHandler {
public:
    TFTPWriteRequestHandler(sockaddr_in client_addr, TFTPRequest request) : TFTPRequestHandler(client_addr), m_request(request) {}
    void handle_request() override {
#ifdef DEBUG
        logger->info("Handling write request from client: " + std::string(inet_ntoa(m_client_addr.sin_addr)) + ":" + std::to_string(ntohs(m_client_addr.sin_port)));
#endif
        int transfer_fd = initialize_transfer_socket();
        if (transfer_fd < 0) {
            logger->info("Failed to initialize transfer socket");
            return;
        }

        std::ofstream file(m_request.filename, std::ios::binary);
        if (!file.is_open()) {
            send_error(transfer_fd, m_client_addr, 2, "Failed to create file");
            close(transfer_fd);
            return;
        }

        // Ack block 0 to start the transfer.
        send_ack(transfer_fd, m_client_addr, m_block_number);
        m_block_number++;

        char block_data_buffer[TFTP_MAX_DATA_PACKET_SIZE];
        while (!m_stop) {
            ssize_t n = -1;
            sockaddr_in from_addr{};
            bool got_valid_packet = false;

            for (int attempt = 0; attempt < MAX_RECV_RETRIES && !m_stop; ++attempt) {
                socklen_t from_len = sizeof(from_addr);
                n = recvfrom(transfer_fd, block_data_buffer, sizeof(block_data_buffer), 0,
                             (sockaddr*)&from_addr, &from_len);
                if (n < 0) {
                    logger->info("Timed out waiting for data (attempt " + std::to_string(attempt + 1) + ")");
                    continue;
                }
                if (n < 4) {
                    logger->info("Received undersized packet, ignoring");
                    continue;
                }
                if (!same_endpoint(from_addr, m_client_addr)) {
                    logger->info("Ignoring packet from unexpected sender");
                    continue;
                }
                got_valid_packet = true;
                break;
            }

            if (!got_valid_packet) {
                logger->info("Giving up on write transfer after repeated timeouts/invalid packets");
                break;
            }

            file.write(block_data_buffer + 4, n - 4);
            logger->info("Received data packet with block number: " + std::to_string(m_block_number) + ", bytes received: " + std::to_string(n - 4));

            send_ack(transfer_fd, m_client_addr, m_block_number);
            m_block_number++;

            if (n < TFTP_MAX_DATA_PACKET_SIZE) {
#ifdef DEBUG
                logger->info("End of data received, transfer complete.");
#endif
                break;
            }
        }

        file.close();
        close(transfer_fd);
    }
    void stop() override {
        m_stop = true;
    }
private:
    std::atomic<bool> m_stop{false};
    uint16_t m_block_number{0};
    TFTPRequest m_request;
};

using TFTPRequestHandlerPtr = std::unique_ptr<TFTPRequestHandler>;

/**
 * @class ThreadPool
 * @brief Simple worker thread pool for processing request handlers.
 */
class ThreadPool {
public:
    ThreadPool(size_t numThreads) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    TFTPRequestHandlerPtr task;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task->handle_request();
                }
            });
        }
    }
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            worker.join();
    }

    void submit(TFTPRequestHandlerPtr task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace(std::move(task));
        }
        condition.notify_one();
    }

    void stop_all() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
    }
private:
    std::vector<std::thread> workers;
    std::queue<TFTPRequestHandlerPtr> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

/**
 * @class Server
 * @brief UDP server that receives TFTP requests and dispatches handlers.
 */
class Server {
public:
    Server(int port) : m_port(port) {}
    int Init() {
        int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            perror("socket");
            return SocketCreateFailed;
        }
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(m_port);

        if (bind(socket_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("bind");
            close(socket_fd);
            return BindFailed;
        }

        set_recv_timeout(socket_fd, TIMEOUT_1Sec);

        m_socket_fd = socket_fd;
        m_thread_pool = std::make_unique<ThreadPool>(NUMBER_OF_WORKERS);
        return ResultSuccess;
    }
    void Run() {
        while (g_stop_flag == 0) {
            char buffer[MAX_BUFFER_SIZE];
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            ssize_t n = recvfrom(
                m_socket_fd,
                buffer,
                sizeof(buffer),
                0,
                (sockaddr*)&client_addr,
                &client_len
            );

            if (n < 0) {
                // Timeout (EAGAIN/EWOULDBLOCK) is expected periodically so
                // we can re-check g_stop_flag; only log real errors.
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("recvfrom");
                }
                continue;
            }
#ifdef DEBUG
            logger->info("Request from " + std::string(inet_ntoa(client_addr.sin_addr)) + ":" + std::to_string(ntohs(client_addr.sin_port)));
            logger->info("Message size:" + std::to_string(n));
#endif

            auto tftp_request = parse_rrq_packet(buffer, n);

            if (OpCode::RRQ == tftp_request.op_code) {
                m_thread_pool->submit(std::make_unique<TFTPReadRequestHandler>(client_addr, tftp_request));
            } else if (OpCode::WRQ == tftp_request.op_code) {
                m_thread_pool->submit(std::make_unique<TFTPWriteRequestHandler>(client_addr, tftp_request));
            } else {
                logger->info("Unknown request type: " + std::to_string(tftp_request.op_code));
                send_error(m_socket_fd, client_addr, 4, "Invalid request");
            }
        }
        m_thread_pool->stop_all();
        close(m_socket_fd);
    }

private:
    int m_port;
    int m_socket_fd{-1}; //Initialized to -1 to indicate uninitialized state
    std::unique_ptr<ThreadPool> m_thread_pool;
};

void signal_handler(int signum) {
    (void)signum;
    g_stop_flag = 1;
}

int main() {
    log4cxx::BasicConfigurator::configure();
    logger->info("Starting TFTP Server...");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    Server server(TFTP_PORT);
    int init_result = server.Init();
    if (init_result != ResultSuccess) {
        logger->info("Server initialization failed with error code: " + std::to_string(init_result));
        return init_result;
    }
    logger->info("Server started successfully.");
    server.Run();
    logger->info("Server shutting down..");
    return 0;
}
