#pragma once
#include <string>

// Registration and semantic metadata only: no setters, rendering, allocation,
// compilation or device probes. Backend facts are supplied by the caller.
std::string TVPTestCapabilityAuditJSON(bool nativeCompiled, bool backendAvailable,
                                     bool deviceDouble);
void CapabilityAuditTests(bool checkFacade);
