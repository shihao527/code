#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <opencv2/opencv.hpp>
#include <boost/asio.hpp>
#include "common.hpp"

namespace udp_streaming
{
    class Receiver
    {
    public:
        Receiver(const std::string &sender_ip, uint16_t sender_port, uint16_t receiver_port)
            : io_context_(),
              socket_(io_context_, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), receiver_port)),
              sender_endpoint_(boost::asio::ip::make_address(sender_ip), sender_port),
              fps_(DEFAULT_FPS),
              stop_(false)
        {
            cv::namedWindow("Live Stream", cv::WINDOW_AUTOSIZE);
            send_request("START", fps_);
            try
            {
                auto local_endpoint = socket_.local_endpoint();
                std::cout << "Receiver bound to: " << local_endpoint.address().to_string()
                          << ":" << local_endpoint.port() << "\n";
            }
            catch (const boost::system::system_error &e)
            {
                std::cerr << "Error getting local endpoint: " << e.what() << "\n";
            }
            // Start packet processing thread
            packet_thread_ = std::thread(&Receiver::process_packets, this);
        }

        ~Receiver()
        {
            {
                std::lock_guard<std::mutex> lock(display_mutex_);
                stop_ = true;
            }
            display_condition_.notify_all();
            io_context_.stop();
            if (packet_thread_.joinable())
                packet_thread_.join();
            send_request("STOP", 0);
            socket_.close();
            cv::destroyWindow("Live Stream");
        }

        void run()
        {
            while (!stop_)
            {
                std::unique_lock<std::mutex> lock(display_mutex_);
                display_condition_.wait_for(lock, std::chrono::milliseconds(100),
                                            [this]
                                            { return stop_ || frame_buffers_sorted_.size() >= 5; });

                if (stop_ && frame_buffers_sorted_.empty())
                    break;

                while (!frame_buffers_sorted_.empty())
                {
                    auto it = frame_buffers_sorted_.begin();
                    auto fb = it->second;

                    if (fb->segments_received != fb->total_segments || fb->data.empty())
                    {
                        frame_buffers_sorted_.erase(it);
                        continue;
                    }

                    frame_buffers_sorted_.erase(it); // Remove before unlock
                    lock.unlock();

                    // Decode and display in main thread
                    cv::Mat rawData(1, fb->total_size, CV_8UC1);
                    std::memcpy(rawData.data, fb->data.data(), fb->total_size);
                    cv::Mat frame = cv::imdecode(rawData, cv::IMREAD_COLOR);

                    if (!frame.empty())
                    {
                        cv::imshow("Live Stream", frame);
                        cv::waitKey(1);
                        std::cout << "Displayed frame " << fb->frame_id
                                  << ", size: " << frame.cols << "x" << frame.rows << "\n";
                    }
                    else
                    {
                        std::cerr << "Failed to decode frame " << fb->frame_id << "\n";
                    }

                    lock.lock();
                }
            }
        }

    private:
        void process_packets()
        {
            start_receive();
            io_context_.run(); // Run event loop in packet thread
        }

        void start_receive()
        {
            receive_buffer_ = std::make_shared<std::vector<uint8_t>>(MAX_PACKET_SIZE);
            socket_.async_receive_from(
                boost::asio::buffer(*receive_buffer_),
                from_endpoint_,
                [this](const boost::system::error_code &error, std::size_t bytes_transferred)
                {
                    if (!error && !stop_)
                    {
                        std::cout << "Received packet of size: " << bytes_transferred << "\n";
                        if (bytes_transferred >= sizeof(PacketHeader))
                        {
                            process_packet(*receive_buffer_, bytes_transferred, from_endpoint_);
                        }
                        else
                        {
                            std::cout << "Packet too small: " << bytes_transferred << "\n";
                        }
                    }
                    else if (error)
                    {
                        std::cerr << "Async receive error: " << error.message() << "\n";
                    }
                    if (!stop_)
                        start_receive();
                });
        }

        void process_packet(const std::vector<uint8_t> &packet_data, size_t len,
                            const boost::asio::ip::udp::endpoint &from_endpoint)
        {
            PacketHeader header;
            std::memcpy(&header, packet_data.data(), sizeof(PacketHeader));
            if (header.type != PacketType::DATA)
                return;

            if (header.total_segments > MAX_SEGMENTS || header.segment >= header.total_segments ||
                header.size > MAX_PACKET_SIZE - sizeof(PacketHeader))
            {
                std::cout << "Invalid packet: segment=" << header.segment
                          << ", total_segments=" << header.total_segments
                          << ", size=" << header.size << "\n";
                return;
            }

            if (len < sizeof(PacketHeader) + header.size)
            {
                std::cout << "Packet truncated: len=" << len << ", expected="
                          << sizeof(PacketHeader) + header.size << "\n";
                return;
            }
            {

                std::lock_guard<std::mutex> lock(frame_buffers_mutex_);
                auto &fb = frame_buffers_[header.frame_id];
                if (!fb)
                {
                    fb = std::make_shared<FrameBuffer>();
                    fb->reset(header.total_segments, header.frame_id);
                    fb->last_received = std::chrono::steady_clock::now();
                }

                if (fb->received_segments[header.segment])
                    return;

                std::memcpy(
                    fb->data.data() + header.segment * (MAX_PACKET_SIZE - sizeof(PacketHeader)),
                    packet_data.data() + sizeof(PacketHeader),
                    header.size);

                fb->received_segments[header.segment] = true;
                fb->segments_received++;
                fb->last_received = std::chrono::steady_clock::now();
                fb->total_size += header.size;
                std::cout << "Frame " << header.frame_id << ": segments_received="
                          << fb->segments_received << ", total_segments=" << fb->total_segments << "\n";

                if (fb->segments_received == fb->total_segments)
                {
                    {
                        std::lock_guard<std::mutex> lock(display_mutex_);
                        frame_buffers_sorted_[fb->frame_id] = fb;
                        std::cout << "Buffered frame " << fb->frame_id
                                  << ", sorted size=" << frame_buffers_sorted_.size() << "\n";
                    }
                    display_condition_.notify_one();
                    frame_buffers_.erase(header.frame_id);
                }
            }
            cleanup_stale_frames();
        }

        void cleanup_stale_frames()
        {
            std::lock_guard<std::mutex> lock(frame_buffers_mutex_);
            auto now = std::chrono::steady_clock::now();
            for (auto it = frame_buffers_.begin(); it != frame_buffers_.end();)
            {
                if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second->last_received).count() > 2)
                {
                    std::cout << "Removed stale frame " << it->second->frame_id << "\n";
                    it = frame_buffers_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        void send_request(const std::string &command, uint8_t fps)
        {
            RequestPacket req;
            req.header.type = PacketType::REQUEST;
            req.fps = fps;
            req.command = command;
            try
            {
                socket_.send_to(
                    boost::asio::buffer(&req, sizeof(PacketHeader) + sizeof(fps) + command.size() + 1),
                    sender_endpoint_);
                std::cout << "Sent " << command << " request to " << sender_endpoint_ << "\n";
            }
            catch (const boost::system::system_error &e)
            {
                std::cerr << "Send request error: " << e.what() << "\n";
            }
        }

        std::unordered_map<uint32_t, std::shared_ptr<FrameBuffer>> frame_buffers_;
        std::mutex frame_buffers_mutex_;
        std::map<uint32_t, std::shared_ptr<FrameBuffer>> frame_buffers_sorted_;
        std::mutex display_mutex_;
        std::condition_variable display_condition_;
        std::thread packet_thread_;
        std::atomic<bool> stop_;
        boost::asio::io_context io_context_;
        boost::asio::ip::udp::socket socket_;
        boost::asio::ip::udp::endpoint sender_endpoint_;
        boost::asio::ip::udp::endpoint from_endpoint_;
        std::shared_ptr<std::vector<uint8_t>> receive_buffer_;
        int fps_;
    };

} // namespace udp_streaming

int main(int argc, char *argv[])
{
    try
    {
        std::string sender_ip = (argc > 1) ? argv[1] : "127.0.0.1";
        uint16_t sender_port = (argc > 2) ? std::atoi(argv[2]) : udp_streaming::SENDER_PORT;
        uint16_t receiver_port = (argc > 3) ? std::atoi(argv[3]) : udp_streaming::RECEIVER_PORT;
        udp_streaming::Receiver receiver(sender_ip, sender_port, receiver_port);
        std::cout << "Receiver started, waiting for packets...\n";
        receiver.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}