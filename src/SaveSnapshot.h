// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Game.h"
#include <DeferredStream.h>
#include <BackgroundFileWriter.h>
#include <ApplicationHost.h>

// Capture only runs on the owner thread; the returned function owns every byte.
template<class Save> GAGCore::BackgroundFileWriter::Encode captureSave(Save save) {
    GAGCore::DeferredStream stream;
    DeferredGameSHA1 sha;
    save(&stream,&sha);
    auto snapshot=std::make_shared<GAGCore::DeferredStream::Snapshot>(stream.takeSnapshot());
    return [snapshot,sha](GAGCore::ChunkedBuffer& bytes) mutable -> GAGCore::CooperativeTask {
        if(!(co_await snapshot->finishTask(bytes))) co_return false;
        sha.start=snapshot->relocate(sha.start); sha.end=snapshot->relocate(sha.end);
        sha.headerOffset=snapshot->relocate(sha.headerOffset); sha.sha1Offset=snapshot->relocate(sha.sha1Offset);
        SHA1_CTX context; SHA1Init(&context);
        // Hash the originally written header, then the finalized body, in bounded slices.
        const auto hash=[&](const unsigned char* data,size_t n){SHA1Update(&context,data,n);};
        bytes.forEachRange(sha.start,sha.headerOffset-sha.start,hash);
        SHA1Update(&context,reinterpret_cast<const unsigned char*>(sha.initialHeader.data()),sha.initialHeader.size());
        for(size_t off=sha.headerOffset+sha.initialHeader.size();off<sha.end;) {
            const size_t n=std::min<size_t>(65536,sha.end-off);
            bytes.forEachRange(off,n,hash);off+=n;
            co_await GAGCore::CooperativeTask::checkpoint("Hashing save");
        }
        unsigned char digest[20];SHA1Final(digest,&context);bytes.writeAt(sha.sha1Offset,digest,20);
        co_return true;
    };
}

// state() is polled on the UI/owner thread. A busy worker delays capture, never
// blocks it or retains a second snapshot. Browser persistence begins after rename.
class SaveOperation final : public GAGCore::ApplicationHost::Persistence {
    GAGCore::BackgroundFileWriter& writer;
    std::string filename;
    mutable std::function<GAGCore::BackgroundFileWriter::Encode()> capture;
    mutable std::function<void()> succeeded;
    mutable std::shared_ptr<GAGCore::BackgroundFileWriter::Result> result;
    mutable std::unique_ptr<GAGCore::ApplicationHost::Persistence> persistence;
    mutable bool failed=false, done=false;
public:
    SaveOperation(GAGCore::BackgroundFileWriter& writer,std::string filename,
                  std::function<GAGCore::BackgroundFileWriter::Encode()> capture,std::function<void()> succeeded)
        :writer(writer),filename(std::move(filename)),capture(std::move(capture)),succeeded(std::move(succeeded)) {}
    GAGCore::ApplicationHost::PersistenceState state() const override {
        using S=GAGCore::ApplicationHost::PersistenceState;
        if(failed) return S::Failed;
        if(done) return S::Succeeded;
        try {
            writer.poll();
            if(!result) {
                if(writer.busy()) return S::Pending;
                result=writer.submit(filename,capture()); capture={};
            }
            if(result->state==0) return S::Pending;
            if(result->state<0) {failed=true;return S::Failed;}
            if(!persistence) persistence=GAGCore::ApplicationHost::persistStorage();
            const auto status=persistence->state();
            if(status==S::Failed) failed=true;
            if(status==S::Succeeded) {succeeded();succeeded={};done=true;}
            return status;
        } catch(...) { failed=true; capture={}; return S::Failed; }
    }
};
