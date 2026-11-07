// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/observation/ResourceInitializationCache.h"
#include "FileFormatVersions.h"

namespace AIEngine
{
bool loadResourceInitializations(GAGCore::InputStream* stream,ResourceInitializations& fields,int owner,size_t expectedCells,int versionMinor)
{
 stream->readEnterSection("resourceInitializations");const Uint32 count=stream->readUint32("count");
 if(count>MaterialSlotCount*MOVEMENT_CLASS_COUNT) {stream->readLeaveSection();return false;}
 for(Uint32 i=0;i<count;++i) {
  stream->readEnterSection(i);const int savedKey=stream->readSint32("key");
  const int classes=versionMinor<FILE_FORMAT_VERSION_ROUTING_RATIO_POLICY?7:MOVEMENT_CLASS_COUNT;
  const int key=(savedKey/classes)*MOVEMENT_CLASS_COUNT+accessClass(movementAccessFromLegacy(savedKey%classes));ResourceInitialization field;
  field.observedTick=stream->readUint32("tick");const Uint32 cells=stream->readUint32("cells");
  if(savedKey<0 || savedKey/(MaterialSlotCount*classes)!=owner || cells!=expectedCells) {stream->readLeaveSection(2);return false;}
  std::vector<Uint8> bytes(size_t(cells)*2);stream->read(bytes.data(),bytes.size(),"values");
  auto values=std::make_shared<std::vector<Uint16>>(cells);
  for(size_t cell=0;cell<cells;++cell) (*values)[cell]=bytes[cell*2]|(Uint16(bytes[cell*2+1])<<8);
  field.values=std::move(values);stream->readLeaveSection();
  if(!fields.emplace(key,std::move(field)).second && versionMinor>=FILE_FORMAT_VERSION_ROUTING_RATIO_POLICY) {stream->readLeaveSection();return false;}
 }
 stream->readLeaveSection();return stream->isValid();
}
}
