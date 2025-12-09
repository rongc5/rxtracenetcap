#ifndef PDEF_WRAPPER_H
#define PDEF_WRAPPER_H

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

extern "C" {
#include "parser.h"
#include "../runtime/protocol.h"
}

namespace pdef {

class ProtocolFilter {
public:
    ProtocolFilter();
    ~ProtocolFilter();

    /* Load and compile a PDEF file */
    bool load(const std::string& path);

    /* Match a packet buffer against all filters */
    bool match(const uint8_t* packet, std::size_t len, uint16_t port) const;
    bool match(const std::vector<uint8_t>& packet, uint16_t port) const;

    /* Print protocol details and filters */
    void print() const;

    /* Accessors */
    const std::string& getName() const;
    std::size_t getFilterCount() const;
    std::vector<uint16_t> getPorts() const;
    const std::string& getError() const;
    bool loaded() const { return proto_ != NULL; }

    /* Release current protocol (if any) */
    void reset();

private:
    /* Non-copyable (C++98 style) */
    ProtocolFilter(const ProtocolFilter&);
    ProtocolFilter& operator=(const ProtocolFilter&);

    ProtocolDef* proto_;
    std::string name_;
    std::string error_;
};

}  // namespace pdef

#endif /* PDEF_WRAPPER_H */
