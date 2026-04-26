#include "DatasetWriter.h"
#include "Order.h"
#include "FileManager.h"
#include "Toolkit.h"

DatasetWriter::DatasetWriter()
	: file(NULL), numRecords(0)
{
}

DatasetWriter::~DatasetWriter()
{
	close();
}

void DatasetWriter::writeU16(Uint16 v)
{
	fwrite(&v, 2, 1, file);
}

void DatasetWriter::writeU32(Uint32 v)
{
	fwrite(&v, 4, 1, file);
}

bool DatasetWriter::open(const std::string& path)
{
	// Match ReplayWriter's absolute-path bypass: FileManager::openFP
	// prepends every dirList entry, which turns absolute paths into
	// nonsense (~/.glob2//tmp/foo). The trainer pipeline relies on
	// arbitrary absolute paths working as written.
	if (!path.empty() && path[0] == '/')
		file = fopen(path.c_str(), "wb");
	else
		file = GAGCore::Toolkit::getFileManager()->openFP(path, "wb");

	if (!file)
		return false;

	numRecords = 0;

	// Header: magic + format_version + num_records placeholder + flags.
	fwrite("GDS1", 4, 1, file);
	writeU32(0); // format_version
	writeU32(0); // num_records placeholder, patched in close()
	writeU32(0); // flags

	return true;
}

void DatasetWriter::writeRecord(Uint32 tick, Order& order)
{
	if (!file)
		return;

	// Skip non-action orders, matching the criteria ReplayWriter::pushOrder
	// uses to filter what reaches the replay stream. ORDER_NULL fires every
	// tick for every player ("did nothing") — without this skip the dataset
	// is dominated by non-actions and num_records balloons (4 players ×
	// 30k ticks ≈ 120k records, of which only a few thousand are real
	// AI decisions).
	Uint8 type = order.getOrderType();
	if (type == ORDER_NULL || type == ORDER_VOICE_DATA)
		return;

	writeU32(tick);
	Uint8 sender = (Uint8)order.sender;
	fwrite(&sender, 1, 1, file);
	fwrite(&type, 1, 1, file);
	writeU16(0); // padding to keep the 4-byte alignment after this point

	// State blob (observation features) — empty in format version 0.
	// Comes online when the trainer's observation representation lands;
	// at that point format_version bumps to 1 and this field carries
	// the serialized state.
	writeU32(0);

	// Order payload, exactly as Order::getData() returns it.
	int payloadLen = order.getDataLength();
	writeU32((Uint32)payloadLen);
	if (payloadLen > 0)
		fwrite(order.getData(), 1, payloadLen, file);

	numRecords++;
}

void DatasetWriter::close()
{
	if (!file)
		return;

	// Patch num_records (offset 8 in the header: after magic[4] + version[4]).
	fseek(file, 8, SEEK_SET);
	writeU32(numRecords);

	fclose(file);
	file = NULL;
}
