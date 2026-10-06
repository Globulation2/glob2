// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <map>
#include <memory>
#include <vector>
#include <Stream.h>
#include "Ressource.h"

namespace AIEngine
{
// A missing shared field is initialized once from one logical observation.
// Retain only this derived plane until the engine publishes its enrolled slot.
struct ResourceInitialization
{
 Uint32 observedTick=0;
 std::shared_ptr<const std::vector<Uint16>> values;
};
using ResourceInitializations=std::map<int,ResourceInitialization>;
inline void saveResourceInitializations(GAGCore::OutputStream* stream,const ResourceInitializations& fields)
{
 stream->writeEnterSection("resourceInitializations");stream->writeUint32(fields.size(),"count");
 unsigned i=0;
 for(const auto& [key,field]:fields) {
  stream->writeEnterSection(i++);stream->writeSint32(key,"key");stream->writeUint32(field.observedTick,"tick");
  stream->writeUint32(field.values->size(),"cells");std::vector<Uint8> bytes(field.values->size()*2);
  for(size_t cell=0;cell<field.values->size();++cell) {bytes[cell*2]=(*field.values)[cell]&255;bytes[cell*2+1]=(*field.values)[cell]>>8;}
  stream->write(bytes.data(),bytes.size(),"values");stream->writeLeaveSection();
 }
 stream->writeLeaveSection();
}
inline bool loadResourceInitializations(GAGCore::InputStream* stream,ResourceInitializations& fields,int owner,size_t expectedCells)
{
 stream->readEnterSection("resourceInitializations");const Uint32 count=stream->readUint32("count");
 if(count>MAX_NB_RESOURCES*7) {stream->readLeaveSection();return false;}
 for(Uint32 i=0;i<count;++i) {
  stream->readEnterSection(i);const int key=stream->readSint32("key");ResourceInitialization field;
  field.observedTick=stream->readUint32("tick");const Uint32 cells=stream->readUint32("cells");
  if(key<0 || key/(MAX_NB_RESOURCES*7)!=owner || cells!=expectedCells) {stream->readLeaveSection(2);return false;}
  std::vector<Uint8> bytes(size_t(cells)*2);stream->read(bytes.data(),bytes.size(),"values");
  auto values=std::make_shared<std::vector<Uint16>>(cells);
  for(size_t cell=0;cell<cells;++cell) (*values)[cell]=bytes[cell*2]|(Uint16(bytes[cell*2+1])<<8);
  field.values=std::move(values);stream->readLeaveSection();
  if(!fields.emplace(key,std::move(field)).second) {stream->readLeaveSection();return false;}
 }
 stream->readLeaveSection();return stream->isValid();
}

}
