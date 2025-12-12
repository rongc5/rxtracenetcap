#include <iostream>
#include <vector>
#include "../src/pdef/pdef_wrapper.h"

int main() {

    pdef::ProtocolFilter filter;
    if (!filter.load("tests/samples/game_with_filter.pdef")) {
        std::cerr << "Error: " << filter.getError() << std::endl;
        return 1;
    }

    std::cout << "=== Protocol Information ===" << std::endl;
    filter.print();
    std::cout << std::endl;


    const uint8_t packet_data[] = {

        0xDE, 0xAD, 0xBE, 0xEF,
        0x01,
        0x01,
        0x00, 0x00,
        0x00, 0x01, 0x86, 0xA0,
        0x00, 0x00, 0x00, 0x01,


        0x00, 0x01, 0x86, 0xA0,
        0x00, 0x32,
        0x01,

        'T', 'e', 's', 't', 'P', 'l', 'a', 'y', 'e', 'r', 0, 0, 0, 0, 0, 0,


        0x00, 0x00, 0x04, 0xD2,
        0x00, 0x00, 0x00, 0x01,
    };

    std::vector<uint8_t> packet(packet_data, packet_data + sizeof(packet_data));


    std::cout << "=== Testing Filters ===" << std::endl;


    bool matched = filter.match(packet.data(), packet.size(), 7777);
    std::cout << "LoginPackets filter: " << (matched ? "MATCHED" : "NOT MATCHED") << std::endl;


    packet[20] = 0x00;
    packet[21] = 0x64;
    matched = filter.match(packet.data(), packet.size(), 7777);
    std::cout << "HighLevelPlayers filter (level=100): " << (matched ? "MATCHED" : "NOT MATCHED") << std::endl;


    packet[20] = 0x00;
    packet[21] = 0x20;
    matched = filter.match(packet.data(), packet.size(), 7777);
    std::cout << "HighLevelPlayers filter (level=32): " << (matched ? "MATCHED" : "NOT MATCHED") << std::endl;


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
