// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// GCF client for the head unit's own connection manager (ConnMngrService / NetworkingService), over /dev/scp_pss.
//
// The same frames and library calls as the factory /opt/conn/bin/sendcall, but the replies are returned whole
// (sendcall only turns the integer `result` into its exit status). Car-proven with tools/aa_ap on the J29:
//   CTRL REGS <me> GCF_Version='1.0' INFO='' VERSION='' CONFIG={  };   CTRL REGA <me> ADDR=<me>;   CTRL REGF <me> ;
//   CALL <me>:<seq> <Method> key='str' key=INT key={ list };
//   reply: RESP <me>:<seq> <Method> <echoed params> ... result=N;
// The car libraries are loaded at run time (libsac.so needs libpal_core.so, which needs StarRec_TraceOut_trace from
// libtools_common.so: open libtools_common first). They resolve through LD_LIBRARY_PATH=/opt/conn/lib, so this is only
// used from the one-shot `nbt-aad --wfd-own` child, which the daemon starts with that set - never inside the daemon.
#ifndef GCF_CLIENT_HPP
#define GCF_CLIENT_HPP

#include <string>
#include <vector>

namespace nbt {

class GcfClient {
public:
    GcfClient();
    // Loads the car libraries, opens /dev/scp_pss and registers as "<name>_<pid>"; false + why on failure.
    bool open(const char* name, std::string& why);
    // One CALL; returns the reply that names our client and the method (empty on timeout). Other messages are
    // skipped. `params` without the trailing ';'.
    std::string call(const std::string& method, const std::string& params, int timeoutSec);
    const std::string& me() const { return me_; }

private:
    std::string me_;
    unsigned seq_;
};

// ---- reply parsing (pure, host-tested) ----------------------------------------------------------------------
// Raw text of `key=value` in a reply: a '...' string with the quotes, a {..} list, or a bare token.
bool gcfField(const std::string& msg, const std::string& key, std::string& value);
// `result=N` of a reply, -9999 when absent.
int gcfResult(const std::string& msg);
// '...' -> ... (unchanged when not quoted).
std::string gcfUnquote(const std::string& v);

} // namespace nbt

#endif // GCF_CLIENT_HPP
