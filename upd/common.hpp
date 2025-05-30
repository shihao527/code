#ifndef COMMON_HPP
#define COMMON_HPP

#include <cstdint>
#include <string>

namespace udp_streaming {

constexpr size_t MAX_PACKET_SIZE = 1400;
constexpr size_t MAX_SEGMENTS = 500;
constexpr size_t DEFAULT_SEGMENTS = 200;
constexpr int DEFAULT_FPS = 30;

constexpr uint16_t SENDER_PORT = 53740;
constexpr uint16_t RECEIVER_PORT = 53840;
// Packet types
enum class PacketType : uint8_t {
    REQUEST = 1, // START or ADJUST
    DATA = 2     // Video frame segment
};

// Packet header
struct PacketHeader {
    PacketType type;      // REQUEST or DATA
    uint32_t seq_num;     // Sequence number
    uint32_t frame_id;    // Frame identifier
    uint16_t segment;     // Segment number (0 to total_segments-1)
    uint16_t total_segments; // Total segments in frame
    uint64_t timestamp;   // Microseconds since epoch
    uint16_t size;
    uint16_t total_size; // Total size of the frame
    PacketHeader() : type(PacketType::DATA), seq_num(0), frame_id(0), segment(0),
                     total_segments(0), timestamp(0), size(0), total_size(0){}
};

// Request packet (START or ADJUST)
struct RequestPacket {
    PacketHeader header;
    uint8_t fps;         // Requested frame rate
    std::string command; // "START" or "ADJUST"

    RequestPacket() : fps(DEFAULT_FPS), command("START") {
        header.type = PacketType::REQUEST;
    }
};

// Data packet
struct DataPacket {
    PacketHeader header;
    std::vector<uint8_t> payload;

    DataPacket() : payload(MAX_PACKET_SIZE - sizeof(PacketHeader)) {
        header.type = PacketType::DATA;
    }
};

// Frame buffer for reassembly
class FrameBuffer {
public:
    FrameBuffer()
    : segments_received(0),
      total_segments(0),
      frame_id(static_cast<uint32_t>(-1)),
      data(MAX_SEGMENTS * (MAX_PACKET_SIZE - sizeof(PacketHeader)))  
{}
    ~FrameBuffer() = default;

    void reset(size_t segment_count, uint32_t new_frame_id) {
        frame_id = new_frame_id;
        total_segments = segment_count;
        segments_received = 0;
        total_size = 0;
        last_received = std::chrono::steady_clock::now();
        received_segments.assign(segment_count, false);  // Reset flags
        
    }

    std::vector<bool> received_segments;
    std::vector<uint8_t> data; // Reassembled frame data
    int segments_received;     // Number of segments received
    int total_segments;        // Total expected segments
    uint32_t frame_id;         // ID of the current frame being assembled
    std::chrono::steady_clock::time_point last_received; // Timestamp of last received segment
    int total_size; // Total size of the frame

};

} // namespace udp_streaming

#endif 