// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <BinaryStream.h>
#include <ChunkedStreamBackend.h>
#include <functional>
#include <map>
#include <CooperativeTask.h>

namespace GAGCore {
// The capture owns all inputs. Encoding callbacks must capture values, never live
// engine objects. Placeholder positions are relocated after worker finalization.
class DeferredStream : public BinaryOutputStream {
public:
    struct Task { size_t offset; std::function<CooperativeTask(OutputStream*)> encode; };
    struct Snapshot {
        ChunkedBuffer literals;
        std::vector<Task> tasks;
        std::map<size_t,size_t> pointers;
        std::vector<std::pair<size_t,size_t>> positions;
        size_t relocate(size_t old) const {
            auto it=std::upper_bound(positions.begin(),positions.end(),old,
                [](size_t value,const auto& item){ return value<item.first; });
            if(it==positions.begin()) return old;
            --it; return it->second+(old-it->first);
        }
        CooperativeTask finishTask(ChunkedBuffer& result) {
            auto* backend=new ChunkedStreamBackend;
            BinaryOutputStream out(backend);
            size_t start=0;
            positions={{0,0}};
            for(auto& task:tasks) {
                while(start<task.offset) {
                    const size_t n=std::min<size_t>(65536,task.offset-start);
                    literals.forEachRange(start,n,[&](const auto* p,size_t len){out.write(p,len,"literal");});start+=n;
                    co_await CooperativeTask::checkpoint("Copying save");
                }
                positions.emplace_back(task.offset,out.getPosition());
                if(!(co_await task.encode(&out))) co_return false;
                task.encode={};
                co_await CooperativeTask::checkpoint("Encoding save");
                start=task.offset+1;
                positions.emplace_back(start,out.getPosition());
            }
            while(start<literals.size()) {
                const size_t n=std::min<size_t>(65536,literals.size()-start);
                literals.forEachRange(start,n,[&](const auto* p,size_t len){out.write(p,len,"literal");});start+=n;
                co_await CooperativeTask::checkpoint("Copying save");
            }
            for(const auto& [offset,target]:pointers) {
                out.seekFromStart(relocate(offset)); out.writeUint32(relocate(target),"offset");
            }
            literals=ChunkedBuffer{};
            result=backend->takeContents();
            co_return true;
        }
        ChunkedBuffer finish() {
            ChunkedBuffer result; if(!finishTask(result).run()) throw std::runtime_error("Snapshot encoding failed"); return result;
        }
    };
    DeferredStream():BinaryOutputStream(new ChunkedStreamBackend) {}
    void defer(std::function<void(OutputStream*)> encode) {
        deferTask([encode=std::move(encode)](OutputStream* out)->CooperativeTask {
            encode(out);co_return true;
        });
    }
    void deferTask(std::function<CooperativeTask(OutputStream*)> encode) {
        tasks.push_back({getPosition(),std::move(encode)});
        writeUint8(0,"placeholder");
    }
    // Only MapHeader contains a serialized stream offset. It is rewritten after
    // the game body, so record the latest value at each pointer location.
    void writeUint32(Uint32 value,const std::string name) override {
        if(name=="mapOffset") pointers[getPosition()]=value;
        BinaryOutputStream::writeUint32(value,name);
    }
    Snapshot takeSnapshot() {
        return {static_cast<ChunkedStreamBackend*>(backend)->takeContents(),std::move(tasks),std::move(pointers),{}};
    }
private:
    std::vector<Task> tasks;
    std::map<size_t,size_t> pointers;
};
}
