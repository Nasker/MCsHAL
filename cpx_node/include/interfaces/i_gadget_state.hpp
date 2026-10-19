#pragma once

#include <stdint.h>
#include "mcshal_protocol.hpp"
#include "domain/gadget_context.hpp"

namespace mcshal {

// State pattern contract for the gadget lifecycle (Idle / Greeting / Active).
// States communicate transitions via ctx.request_state; the orchestrator
// performs the actual switch so states never hold pointers to each other.
class IGadgetState {
public:
    virtual ~IGadgetState() = default;
    virtual GadgetStateId id() const = 0;
    virtual void onEnter(GadgetContext &ctx) = 0;
    virtual void tick(GadgetContext &ctx, uint32_t now_ms) = 0;
    virtual void onExit(GadgetContext &ctx) = 0;
};

} // namespace mcshal
