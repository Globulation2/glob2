// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace gradient_kernel
{
// Single background producer. A positive old event cannot prove completion of
// newer commands, and an enqueue error cannot prove command rejection.
class OpenCLTransferProof
{
    bool pending=false,ambiguous=false,afterEvent=false;
public:
    void enqueue(int result,bool event,bool blocking=false) noexcept {
        pending=true;
        if(result || (!event && !blocking)) {
            // Normal fire-and-forget kernels/fills are tracked separately by
            // enqueueUnobserved. This path requested a transfer event.
            ambiguous=true;afterEvent=true;
        } else if(blocking) {
            // A successful blocking transfer proves preceding queue work even
            // if it did not request an event.
            if(!result)drained();
        } else afterEvent=false;
    }
    void enqueueUnobserved(int result) noexcept {
        pending=true;afterEvent=true;if(result)ambiguous=true;
    }
    bool needsProof() const noexcept {return pending || ambiguous;}
    bool canPollEvent() const noexcept {return pending && !ambiguous && !afterEvent;}
    void eventCompleted() noexcept {if(canPollEvent())pending=false;}
    // Only a successful blocking command or separately verified queue proof may call
    // this. Failed/negative/unknown completion leaves every flag intact.
    void drained() noexcept {pending=false;ambiguous=false;afterEvent=false;}
};
}
