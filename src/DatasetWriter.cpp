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

	// Header: magic + num_records placeholder. No version field — there's
	// only one producer and one consumer (in this repo) and regenerating
	// datasets is cheap, so we'd never need to support multiple versions
	// in flight. If the format ever changes wire-incompatibly, bump the
	// magic to "GDS2" and parsers reject by magic mismatch.
	fwrite("GDS1", 4, 1, file);
	writeU32(0); // num_records placeholder, patched in close()

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

	// State blob (observation features) — currently empty (length 0).
	// Populated when the trainer's observation representation lands; at
	// that point this header byte changes from 4 zero bytes to a real
	// length-prefixed serialized state. Length-prefixed so the parser
	// can read past it without knowing the schema.
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

	// Patch num_records at offset 4 (right after the magic).
	fseek(file, 4, SEEK_SET);
	writeU32(numRecords);

	fclose(file);
	file = NULL;
}
