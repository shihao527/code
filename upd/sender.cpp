#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <opencv2/opencv.hpp>
#include <boost/asio.hpp>
#include "common.hpp"

namespace udp_streaming
{
    class ThreadPool
    {
    public:
        ThreadPool(size_t num_threads) : stop_(false)
        {
            for (size_t i = 0; i < num_threads; ++i)
            {
                workers_.emplace_back([this]
                                      {
                    while (true)
                    {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(mutex_);
                            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                            if (stop_ && tasks_.empty())
                                return;
                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }
                        task();
                    } });
            }
        }

        ~ThreadPool()
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                stop_ = true;
            }
            condition_.notify_all();
            for (auto &worker : workers_)
            {
                if (worker.joinable())
                    worker.join();
            }
        }

        void enqueue(std::function<void()> task)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                tasks_.push(std::move(task));
            }
            condition_.notify_one();
        }

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::atomic<bool> stop_;
    };

    class Sender
    {
    public:
        Sender(const std::string &receiver_ip, uint16_t port)
            : io_context_(),
              socket_(io_context_, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)),
              receiver_endpoint_(boost::asio::ip::make_address(receiver_ip), port),
              fps_(DEFAULT_FPS),
              frame_id_(0),
              seq_num_(0),
              thread_pool_(std::thread::hardware_concurrency())
        {
            capture_ = cv::VideoCapture(0);
            if (!capture_.isOpened())
            {
                std::cerr << "Error: Could not open camera\n";
                return;
            }
            // Print bound address and port
            try
            {
                auto local_endpoint = socket_.local_endpoint();
                std::cout << "Sender bound to: " << local_endpoint.address().to_string()
                          << ":" << local_endpoint.port() << "\n";
            }
            catch (const boost::system::system_error &e)
            {
                std::cerr << "Error getting local endpoint: " << e.what() << "\n";
            }
        }

        ~Sender()
        {
            socket_.close();
            cv::destroyAllWindows();
            capture_.release();
        }

        void send_frame_segments(const std::shared_ptr<std::vector<uint8_t>> &data_ptr,
                                 const boost::asio::ip::udp::endpoint &endpoint)
        {
            const std::vector<uint8_t> &data = *data_ptr;
            size_t size = data.size();
            size_t segment_size = MAX_PACKET_SIZE - sizeof(PacketHeader);
            size_t total_segments = (size + segment_size - 1) / segment_size;

            if (total_segments > MAX_SEGMENTS)
            {
                std::cerr << "Frame too large: " << total_segments << " segments\n";
                return;
            }

            for (size_t i = 0; i < total_segments; ++i)
            {
                
                auto pkt = std::make_shared<DataPacket>();
                {
                    std::lock_guard<std::mutex> lock(seq_num_mutex_);
                    pkt->header.seq_num = seq_num_++;
                }
                pkt->header.frame_id = frame_id_;
                pkt->header.type = PacketType::DATA;
                pkt->header.segment = i;
                pkt->header.total_segments = total_segments;
                pkt->header.total_size = static_cast<uint16_t>(size);
                pkt->header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();

                size_t offset = i * segment_size;
                size_t bytes_to_send = std::min(segment_size, size - offset);
                pkt->header.size = static_cast<uint16_t>(bytes_to_send);
                size_t len = (i == total_segments - 1) ? size - offset : segment_size;
                pkt->payload.resize(len);
                std::memcpy(pkt->payload.data(), data.data() + offset, len);
                std::vector<uint8_t> buffer(sizeof(PacketHeader) + len);
                std::memcpy(buffer.data(), &pkt->header, sizeof(PacketHeader));
                std::memcpy(buffer.data() + sizeof(PacketHeader), pkt->payload.data(), len);
                std::cout << "Sending segment " << i + 1 << "/" << total_segments
          << " of frame " << frame_id_ << " to " << endpoint << "\n";
                thread_pool_.enqueue([this, buffer = std::move(buffer), endpoint]()
                                     {
                                         try
                                         {
                                             socket_.send_to(boost::asio::buffer(buffer), endpoint);
                                         }
                                         catch (const boost::system::system_error &e)
                                         {
                                             std::cerr << "Send error to " << endpoint << ": " << e.what() << "\n";
                                         } });
            }

            std::cout << "Sent frame " << frame_id_ << ", " << total_segments << " segments to " << endpoint << "\n";
            frame_id_++;
        }

        void run()
        {
            std::thread control_thread(&Sender::handle_control_packets, this);
            control_thread.detach();

            while (true)
            {
                cv::Mat frame;
                auto start = std::chrono::high_resolution_clock::now();
                if (!capture_.read(frame))
                {
                    std::cerr << "Failed to capture frame\n";
                    break;
                }
                auto end = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                cv::imshow("Captured Frame", frame);
                if (cv::waitKey(1) == 'q')
                    break;

                auto data_ptr = std::make_shared<std::vector<uint8_t>>();
                cv::imencode(".jpg", frame, *data_ptr);

                const size_t batch_size = 20;
                auto batch = std::make_shared<std::vector<boost::asio::ip::udp::endpoint>>();
                batch->reserve(batch_size);

                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    for (const auto &[endpoint, session] : clients_)
                    {
                        if (session.is_streaming)
                            batch->push_back(endpoint);
                        if (batch->size() >= batch_size)
                        {
                            thread_pool_.enqueue([this, data_ptr, batch]()
                                                 {
                                                     for (const auto &endpoint : *batch)
                                                     {
                                                         send_frame_segments(data_ptr, endpoint);
                                                     } });
                            batch = std::make_shared<std::vector<boost::asio::ip::udp::endpoint>>();
                            batch->reserve(batch_size);
                        }
                    }
                }
                if (!batch->empty())
                {
                    thread_pool_.enqueue([this, data_ptr, batch]()
                                         {
                                             for (const auto &endpoint : *batch)
                                             {
                                                 send_frame_segments(data_ptr, endpoint);
                                             } });
                }

                int min_fps = fps_;
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    for (const auto &[endpoint, session] : clients_)
                    {
                        if (session.is_streaming && session.fps < min_fps)
                            min_fps = session.fps;
                    }
                }

                int64_t wait_time = 1000000 / min_fps - elapsed;
                if (wait_time > 0)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(wait_time));
                }
            }
        }

    private:
        struct ClientSession
        {
            std::chrono::steady_clock::time_point last_seen;
            bool is_streaming;
            int fps;
        };

        void handle_control_packets()
        {
            boost::asio::ip::udp::endpoint sender_endpoint;
            RequestPacket req;

            while (true)
            {
                try
                {
                    size_t len = socket_.receive_from(boost::asio::buffer(&req, sizeof(req)), sender_endpoint);
                    if (len >= sizeof(PacketHeader) && req.header.type == PacketType::REQUEST)
                    {
                        std::lock_guard<std::mutex> lock(clients_mutex_);

                        auto now = std::chrono::steady_clock::now();
                        auto &session = clients_[sender_endpoint];
                        session.last_seen = now;

                        if (req.command == "START")
                        {
                            session.is_streaming = true;
                            session.fps = (req.fps > 0 && req.fps <= 60) ? req.fps : DEFAULT_FPS;
                            std::cout << "START from " << sender_endpoint << ", fps=" << session.fps << "\n";
                        }
                        else if (req.command == "ADJUST")
                        {
                            session.fps = (req.fps > 0 && req.fps <= 60) ? req.fps : DEFAULT_FPS;
                            std::cout << "ADJUST from " << sender_endpoint << ", fps=" << session.fps << "\n";
                        }
                        else if (req.command == "STOP")
                        {
                            session.is_streaming = false;
                            std::cout << "STOP from " << sender_endpoint << "\n";
                        }
                    }
                }
                catch (const boost::system::system_error &e)
                {
                    std::cerr << "Control packet error: " << e.what() << "\n";
                }
            }
        }

    private:
        std::mutex seq_num_mutex_;
        boost::asio::io_context io_context_;
        boost::asio::ip::udp::socket socket_;
        boost::asio::ip::udp::endpoint receiver_endpoint_;
        cv::VideoCapture capture_;
        int fps_;
        uint32_t frame_id_;
        uint32_t seq_num_;
        std::unordered_map<boost::asio::ip::udp::endpoint, ClientSession> clients_;
        std::mutex clients_mutex_;
        ThreadPool thread_pool_;
    };

} // namespace udp_streaming

int main(int argc, char *argv[])
{
    try
    {
        std::string receiver_ip = (argc > 1) ? argv[1] : "127.0.0.1";
        udp_streaming::Sender sender(receiver_ip, udp_streaming::SENDER_PORT);
        sender.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}