#include <iostream>
#include <vector>
#include "../src/pdef/pdef_wrapper.h"

int main() {
    // 1. Load protocol definition
    pdef::ProtocolFilter filter;
    if (!filter.load("tests/samples/game_with_filter.pdef")) {
        std::cerr << "Error: " << filter.getError() << std::endl;
        return 1;
    }

    std::cout << "=== Protocol Information ===" << std::endl;
    filter.print();
    std::cout << std::endl;

    // 2. Simulate packet data
    const uint8_t packet_data[] = {
        /* Header */
        0xDE, 0xAD, 0xBE, 0xEF,  /* magic = 0xDEADBEEF */
        0x01,                    /* version = 1 */
        0x01,                    /* packet_type = TYPE_LOGIN (1) */
        0x00, 0x00,              /* flags */
        0x00, 0x01, 0x86, 0xA0,  /* player_id = 100000 */
        0x00, 0x00, 0x00, 0x01,  /* sequence = 1 */

        /* Player */
        0x00, 0x01, 0x86, 0xA0,  /* player_id = 100000 */
        0x00, 0x32,              /* level = 50 */
        0x01,                    /* status = 1 */
        /* nickname (16 bytes) */
        'T', 'e', 's', 't', 'P', 'l', 'a', 'y', 'e', 'r', 0, 0, 0, 0, 0, 0,

        /* GamePacket */
        0x00, 0x00, 0x04, 0xD2,  /* room_id = 1234 */
        0x00, 0x00, 0x00, 0x01,  /* timestamp = 1 */
    };

    std::vector<uint8_t> packet(packet_data, packet_data + sizeof(packet_data));

    // 3. Test different filters
    std::cout << "=== Testing Filters ===" << std::endl;

    // Test LoginPackets filter
    bool matched = filter.match(packet.data(), packet.size(), 7777);
    std::cout << "LoginPackets filter: " << (matched ? "MATCHED" : "NOT MATCHED") << std::endl;

    // Modify packet to test HighLevelPlayers filter
    packet[20] = 0x00;  // level high byte
    packet[21] = 0x64;  // level low byte = 100
    matched = filter.match(packet.data(), packet.size(), 7777);
    std::cout << "HighLevelPlayers filter (level=100): " << (matched ? "MATCHED" : "NOT MATCHED") << std::endl;

    // Change level to below threshold
    packet[20] = 0x00;  // level high byte
    packet[21] = 0x20;  // level low byte = 32
    matched = filter.match(packet.data(), packet.size(), 7777);
    std::cout << "HighLevelPlayers filter (level=32): " << (matched ? "MATCHED" : "NOT MATCHED") << std::endl;

    // Test with wrong port
    matched = filter.match(packet.data(), packet.size(), 9999);
    std::cout << "Wrong port (9999): " << (matched ? "MATCHED" : "NOT MATCHED") << std::endl;

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Protocol: " << filter.getName() << std::endl;
    std::cout << "Filter rules: " << filter.getFilterCount() << std::endl;
    std::cout << "Configured ports: ";
    std::vector<uint16_t> ports = filter.getPorts();
    for (std::size_t i = 0; i < ports.size(); i++) {
        std::cout << ports[i];
        if (i < ports.size() - 1) std::cout << ", ";
    }
    std::cout << std::endl;

    return 0;
}
