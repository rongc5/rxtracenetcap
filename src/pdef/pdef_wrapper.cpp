#include "pdef_wrapper.h"

#include <limits>

namespace pdef {

ProtocolFilter::ProtocolFilter() : proto_(NULL) {}

ProtocolFilter::~ProtocolFilter() {
    reset();
}

bool ProtocolFilter::load(const std::string& path) {
    reset();
    char err[512] = {0};

    proto_ = pdef_parse_file(path.c_str(), err, sizeof(err));
    if (!proto_) {
        error_.assign(err);
        return false;
    }

    name_.assign(proto_->name);
    error_.clear();
    return true;
}

bool ProtocolFilter::match(const uint8_t* packet, std::size_t len, uint16_t port) const {
    if (!proto_) {
        return false;
    }

    if (len > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    return packet_filter_match(packet, static_cast<uint32_t>(len), port, proto_);
}

bool ProtocolFilter::match(const std::vector<uint8_t>& packet, uint16_t port) const {
    if (packet.empty()) {
        return match(static_cast<const uint8_t*>(NULL), 0, port);
    }
    return match(packet.data(), packet.size(), port);
}

void ProtocolFilter::print() const {
    if (proto_) {
        protocol_print(proto_);
    }
}

const std::string& ProtocolFilter::getName() const {
    return name_;
}

std::size_t ProtocolFilter::getFilterCount() const {
    return proto_ ? proto_->filter_count : 0;
}

std::vector<uint16_t> ProtocolFilter::getPorts() const {
    std::vector<uint16_t> ports;
    if (!proto_ || proto_->port_count == 0 || !proto_->ports) {
        return ports;
    }

    ports.reserve(proto_->port_count);
    for (uint32_t i = 0; i < proto_->port_count; i++) {
        ports.push_back(proto_->ports[i]);
    }
    return ports;
}

const std::string& ProtocolFilter::getError() const {
    return error_;
}

void ProtocolFilter::reset() {
    if (proto_) {
        protocol_free(proto_);
        proto_ = NULL;
    }
    name_.clear();
    error_.clear();
}

}
