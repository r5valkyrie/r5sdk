//=============================================================================//
//
// Purpose: Datatable disk loading system - CSV override for RPak datatables
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "filesystem/filesystem.h"
#include "datatable.h"
#include <unordered_map>
#include <vector>
#include <string>

//-----------------------------------------------------------------------------
// Limits
//-----------------------------------------------------------------------------
static constexpr size_t DATATABLE_MAX_FILE_SIZE    = 10 * 1024 * 1024; // 10 MB
static constexpr int    DATATABLE_MAX_ROWS         = 10000;
static constexpr int    DATATABLE_MAX_COLUMNS      = 100;
static constexpr size_t DATATABLE_MAX_DISK_ENTRIES = 100;
static constexpr size_t DATATABLE_MAX_PATH_LEN     = 256;

//-----------------------------------------------------------------------------
// Disk datatable cache
//-----------------------------------------------------------------------------
static std::unordered_map<uint64_t, DiskDatatable*> g_diskDatatables;
static bool g_diskDatatablesScanned = false;

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
static DiskDatatable* Datatable_ParseCSV(const char* csvPath, const char* rpakPath);
static void Datatable_InsertIntoAssetHashTable(uint64_t guid, DataTableHeader* header);
static void Datatable_LoadAllDiskFiles();
static void Datatable_FreeAll();

//=============================================================================
// CSV PARSING
//=============================================================================

static int Datatable_GetColumnTypeSize(int type)
{
	switch (type)
	{
	case DTCOL_BOOL:             return 4;
	case DTCOL_INT:              return 4;
	case DTCOL_FLOAT:            return 4;
	case DTCOL_VECTOR:           return 12;
	case DTCOL_STRING:           return 8;
	case DTCOL_ASSET:            return 8;
	case DTCOL_ASSET_NOPRECACHE: return 8;
	default:                     return 0;
	}
}

static int Datatable_ParseColumnType(const char* typeName)
{
	if (!typeName)
		return -1;

	if (_stricmp(typeName, "bool") == 0)             return DTCOL_BOOL;
	if (_stricmp(typeName, "int") == 0)              return DTCOL_INT;
	if (_stricmp(typeName, "float") == 0)            return DTCOL_FLOAT;
	if (_stricmp(typeName, "vector") == 0)           return DTCOL_VECTOR;
	if (_stricmp(typeName, "string") == 0)           return DTCOL_STRING;
	if (_stricmp(typeName, "asset") == 0)            return DTCOL_ASSET;
	if (_stricmp(typeName, "asset_noprecache") == 0) return DTCOL_ASSET_NOPRECACHE;

	if (strlen(typeName) == 1 && typeName[0] >= '0' && typeName[0] <= '6')
		return typeName[0] - '0';

	return -1;
}

static std::vector<std::string> Datatable_SplitCSVLine(const char* line)
{
	std::vector<std::string> tokens;
	std::string current;
	bool inQuotes = false;

	while (*line)
	{
		if (*line == '\r' || *line == '\n')
		{
			line++;
			continue;
		}

		if (inQuotes)
		{
			if (*line == '"')
			{
				if (*(line + 1) == '"')
				{
					current += '"';
					line += 2;
				}
				else
				{
					inQuotes = false;
					line++;
				}
			}
			else
			{
				current += *line;
				line++;
			}
		}
		else
		{
			if (*line == '"')
			{
				inQuotes = true;
				line++;
			}
			else if (*line == ',')
			{
				tokens.push_back(current);
				current.clear();
				line++;
			}
			else
			{
				current += *line;
				line++;
			}
		}
	}

	tokens.push_back(current);
	return tokens;
}

static bool Datatable_ParseVector(const char* str, float* out)
{
	while (*str == '<' || *str == ' ' || *str == '\t')
		str++;

	if (sscanf(str, "%f %f %f", &out[0], &out[1], &out[2]) == 3)
		return true;
	if (sscanf(str, "%f , %f , %f", &out[0], &out[1], &out[2]) == 3)
		return true;
	if (sscanf(str, "%f,%f,%f", &out[0], &out[1], &out[2]) == 3)
		return true;

	return false;
}

static DiskDatatable* Datatable_ParseCSV(const char* csvPath, const char* rpakPath)
{
	if (!FileSystem())
		return nullptr;

	FileHandle_t hFile = FileSystem()->Open(csvPath, "r", "GAME");
	if (!hFile)
		return nullptr;

	const ssize_t fileSize = FileSystem()->Size(hFile);
	if (fileSize <= 0 || static_cast<size_t>(fileSize) > DATATABLE_MAX_FILE_SIZE)
	{
		FileSystem()->Close(hFile);
		if (fileSize > 0)
			Warning(eDLL_T::ENGINE, "[DiskDatatable] File too large: %s (%zd bytes, max %zu)\n",
				csvPath, fileSize, DATATABLE_MAX_FILE_SIZE);
		return nullptr;
	}

	char* fileBuffer = new char[fileSize + 1];
	FileSystem()->Read(fileBuffer, fileSize, hFile);
	fileBuffer[fileSize] = '\0';
	FileSystem()->Close(hFile);

	std::vector<std::string> lines;
	{
		char* lineStart = fileBuffer;
		for (char* p = fileBuffer; ; p++)
		{
			if (*p == '\n' || *p == '\0')
			{
				std::string line(lineStart, p - lineStart);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (!line.empty())
					lines.push_back(line);
				if (*p == '\0')
					break;
				lineStart = p + 1;
			}
		}
	}

	delete[] fileBuffer;

	if (lines.size() < 2)
	{
		Warning(eDLL_T::ENGINE, "[DiskDatatable] CSV needs at least 2 lines: %s\n", csvPath);
		return nullptr;
	}

	std::vector<std::string> colNames = Datatable_SplitCSVLine(lines[0].c_str());
	const int columnCount = static_cast<int>(colNames.size());

	if (columnCount <= 0 || columnCount > DATATABLE_MAX_COLUMNS)
	{
		Warning(eDLL_T::ENGINE, "[DiskDatatable] Invalid column count %d (max %d): %s\n",
			columnCount, DATATABLE_MAX_COLUMNS, csvPath);
		return nullptr;
	}

	// Detect type line: Format A (line 2) or Format B (last line)
	std::vector<std::string> colTypeStrs;
	int dataStartLine = -1;
	int dataEndLine = -1;

	// Try Format A: types on line 2
	{
		std::vector<std::string> candidate = Datatable_SplitCSVLine(lines[1].c_str());
		bool allValid = (static_cast<int>(candidate.size()) == columnCount);

		if (allValid)
		{
			for (int i = 0; i < static_cast<int>(candidate.size()); i++)
			{
				std::string t = candidate[i];
				size_t s = t.find_first_not_of(" \t\"");
				size_t e = t.find_last_not_of(" \t\"");
				if (s != std::string::npos) t = t.substr(s, e - s + 1);

				if (Datatable_ParseColumnType(t.c_str()) < 0)
				{
					allValid = false;
					break;
				}
			}
		}

		if (allValid)
		{
			colTypeStrs = candidate;
			dataStartLine = 2;
			dataEndLine = static_cast<int>(lines.size());
		}
	}

	// Try Format B: types on last line
	if (colTypeStrs.empty())
	{
		int lastIdx = static_cast<int>(lines.size()) - 1;
		std::vector<std::string> candidate = Datatable_SplitCSVLine(lines[lastIdx].c_str());
		bool allValid = (static_cast<int>(candidate.size()) == columnCount);

		if (allValid)
		{
			for (int i = 0; i < static_cast<int>(candidate.size()); i++)
			{
				std::string t = candidate[i];
				size_t s = t.find_first_not_of(" \t\"");
				size_t e = t.find_last_not_of(" \t\"");
				if (s != std::string::npos) t = t.substr(s, e - s + 1);

				if (Datatable_ParseColumnType(t.c_str()) < 0)
				{
					allValid = false;
					break;
				}
			}
		}

		if (allValid)
		{
			colTypeStrs = candidate;
			dataStartLine = 1;
			dataEndLine = lastIdx;
		}
	}

	if (colTypeStrs.empty())
	{
		Warning(eDLL_T::ENGINE, "[DiskDatatable] No valid type line found: %s\n", csvPath);
		return nullptr;
	}

	// Resolve column types and compute byte offsets
	std::vector<int> colTypes(columnCount);
	std::vector<int> colByteOffsets(columnCount);
	int rowSize = 0;

	for (int i = 0; i < columnCount; i++)
	{
		std::string typeStr = colTypeStrs[i];
		size_t start = typeStr.find_first_not_of(" \t\"");
		size_t end = typeStr.find_last_not_of(" \t\"");
		if (start != std::string::npos)
			typeStr = typeStr.substr(start, end - start + 1);

		colTypes[i] = Datatable_ParseColumnType(typeStr.c_str());
		if (colTypes[i] < 0)
		{
			Warning(eDLL_T::ENGINE, "[DiskDatatable] Unknown type '%s' for column '%s': %s\n",
				typeStr.c_str(), colNames[i].c_str(), csvPath);
			return nullptr;
		}

		colByteOffsets[i] = rowSize;
		rowSize += Datatable_GetColumnTypeSize(colTypes[i]);
	}

	const int rowCount = dataEndLine - dataStartLine;
	if (rowCount < 0 || rowCount > DATATABLE_MAX_ROWS)
	{
		Warning(eDLL_T::ENGINE, "[DiskDatatable] Invalid row count %d (max %d): %s\n",
			rowCount, DATATABLE_MAX_ROWS, csvPath);
		return nullptr;
	}

	// Calculate string pool size
	size_t stringPoolSize = 0;

	for (int i = 0; i < columnCount; i++)
	{
		std::string trimmed = colNames[i];
		size_t s = trimmed.find_first_not_of(" \t\"");
		size_t e = trimmed.find_last_not_of(" \t\"");
		if (s != std::string::npos)
			trimmed = trimmed.substr(s, e - s + 1);
		else
			trimmed.clear();
		colNames[i] = trimmed;
		stringPoolSize += colNames[i].size() + 1;
	}

	std::vector<std::vector<std::string>> parsedRows;
	parsedRows.reserve(rowCount);

	for (int r = 0; r < rowCount; r++)
	{
		std::vector<std::string> cells = Datatable_SplitCSVLine(lines[dataStartLine + r].c_str());
		parsedRows.push_back(cells);

		for (int c = 0; c < columnCount && c < static_cast<int>(cells.size()); c++)
		{
			if (colTypes[c] == DTCOL_STRING || colTypes[c] == DTCOL_ASSET || colTypes[c] == DTCOL_ASSET_NOPRECACHE)
			{
				std::string trimmed = cells[c];
				size_t s = trimmed.find_first_not_of(" \t");
				size_t e = trimmed.find_last_not_of(" \t");
				if (s != std::string::npos)
					trimmed = trimmed.substr(s, e - s + 1);
				else
					trimmed.clear();
				stringPoolSize += trimmed.size() + 1;
			}
		}
	}

	// Check for size overflow
	const size_t columnDataSize = sizeof(DTColumnData) * columnCount;
	const size_t rowDataSize = static_cast<size_t>(rowCount) * rowSize;

	if (columnDataSize / sizeof(DTColumnData) != static_cast<size_t>(columnCount) ||
		rowDataSize / rowSize != static_cast<size_t>(rowCount))
	{
		Warning(eDLL_T::ENGINE, "[DiskDatatable] Size overflow: %s\n", csvPath);
		return nullptr;
	}

	const size_t totalSize = columnDataSize + rowDataSize + stringPoolSize;
	if (totalSize < columnDataSize || totalSize < rowDataSize)
	{
		Warning(eDLL_T::ENGINE, "[DiskDatatable] Size overflow: %s\n", csvPath);
		return nullptr;
	}

	char* memBlock = new char[totalSize];
	memset(memBlock, 0, totalSize);

	DTColumnData* columnData = reinterpret_cast<DTColumnData*>(memBlock);
	char* rowBuffer = memBlock + columnDataSize;
	char* stringPool = memBlock + columnDataSize + rowDataSize;
	char* stringCursor = stringPool;

	// Fill column descriptors
	for (int i = 0; i < columnCount; i++)
	{
		memcpy(stringCursor, colNames[i].c_str(), colNames[i].size() + 1);
		columnData[i].name = stringCursor;
		stringCursor += colNames[i].size() + 1;
		columnData[i].type = colTypes[i];
		columnData[i].byteOffset = colByteOffsets[i];
	}

	// Fill row data
	for (int r = 0; r < rowCount; r++)
	{
		const std::vector<std::string>& cells = parsedRows[r];

		for (int c = 0; c < columnCount; c++)
		{
			char* cellPtr = rowBuffer + (r * rowSize) + colByteOffsets[c];

			std::string cellVal;
			if (c < static_cast<int>(cells.size()))
			{
				cellVal = cells[c];
				size_t s = cellVal.find_first_not_of(" \t");
				size_t e = cellVal.find_last_not_of(" \t");
				if (s != std::string::npos)
					cellVal = cellVal.substr(s, e - s + 1);
				else
					cellVal.clear();
			}

			switch (colTypes[c])
			{
			case DTCOL_BOOL:
			{
				int val = 0;
				if (_stricmp(cellVal.c_str(), "true") == 0 || cellVal == "1")
					val = 1;
				*(int*)cellPtr = val;
				break;
			}
			case DTCOL_INT:
				*(int*)cellPtr = atoi(cellVal.c_str());
				break;
			case DTCOL_FLOAT:
				*(float*)cellPtr = static_cast<float>(atof(cellVal.c_str()));
				break;
			case DTCOL_VECTOR:
			{
				float vec[3] = { 0.0f, 0.0f, 0.0f };
				Datatable_ParseVector(cellVal.c_str(), vec);
				*(float*)(cellPtr + 0) = vec[0];
				*(float*)(cellPtr + 4) = vec[1];
				*(float*)(cellPtr + 8) = vec[2];
				break;
			}
			case DTCOL_STRING:
			case DTCOL_ASSET:
			case DTCOL_ASSET_NOPRECACHE:
				memcpy(stringCursor, cellVal.c_str(), cellVal.size() + 1);
				*(const char**)cellPtr = stringCursor;
				stringCursor += cellVal.size() + 1;
				break;
			default:
				break;
			}
		}
	}

	DiskDatatable* dt = new DiskDatatable;
	memset(dt, 0, sizeof(DiskDatatable));

	dt->header.columnCount = columnCount;
	dt->header.rowCount = rowCount;
	dt->header.columnData = columnData;
	dt->header.rows = rowBuffer;
	dt->header.dataTableCRC = 0;
	dt->header.rowSize = rowSize;

	dt->memBlock = memBlock;
	dt->memBlockSize = totalSize;
	dt->insertedInHashTable = false;

	V_strncpy(dt->rpakPath, rpakPath, sizeof(dt->rpakPath));

	return dt;
}

//=============================================================================
// HASH TABLE
//=============================================================================

static void Datatable_InsertIntoAssetHashTable(uint64_t guid, DataTableHeader* header)
{
	if (!g_pPakAssetHashTable)
		return;

	uint64_t* table = reinterpret_cast<uint64_t*>(g_pPakAssetHashTable);
	uint32_t slot = static_cast<uint32_t>(guid & 0x3FFFF);

	for (uint32_t i = 0; i < 0x40000; i++)
	{
		uint32_t idx = (slot + i) & 0x3FFFF;
		uint64_t storedGuid = table[4 * idx + 512];

		if (storedGuid == guid)
		{
			table[4 * idx + 514] = reinterpret_cast<uint64_t>(header);
			return;
		}

		if (storedGuid == 0)
		{
			table[4 * idx + 512] = guid;
			table[4 * idx + 514] = reinterpret_cast<uint64_t>(header);
			return;
		}
	}
}

static void Datatable_InvalidateScriptCache()
{
	if (g_pDatatableGuidCache)
		*g_pDatatableGuidCache = 0;
}

//=============================================================================
// DISK SCANNING
//=============================================================================

static int Datatable_LoadSingleFile(const char* csvPath, const char* subdir, const char* filename)
{
	if (g_diskDatatables.size() >= DATATABLE_MAX_DISK_ENTRIES)
		return 0;

	char stem[DATATABLE_MAX_PATH_LEN];
	V_StripExtension(filename, stem, sizeof(stem));

	char fullRpakPath[DATATABLE_MAX_PATH_LEN * 2];
	V_snprintf(fullRpakPath, sizeof(fullRpakPath), "datatable/%s%s.rpak", subdir, stem);

	uint64_t guid;
	bool isGuidFilename = false;

	// Support 0x<GUID>.csv filenames for direct GUID overrides
	if (stem[0] == '0' && (stem[1] == 'x' || stem[1] == 'X'))
	{
		char* endPtr;
		uint64_t parsedGuid = strtoull(stem + 2, &endPtr, 16);
		if (*endPtr == '\0' && endPtr != stem + 2)
		{
			guid = parsedGuid;
			isGuidFilename = true;
		}
	}

	if (!isGuidFilename)
	{
		if ((reinterpret_cast<uintptr_t>(fullRpakPath) & 3) == 0)
			guid = v_HashNameAligned(fullRpakPath);
		else
			guid = v_HashNameUnaligned(fullRpakPath);
	}

	auto it = g_diskDatatables.find(guid);
	if (it != g_diskDatatables.end())
	{
		delete[] it->second->memBlock;
		delete it->second;
		g_diskDatatables.erase(it);
	}

	DiskDatatable* dt = Datatable_ParseCSV(csvPath, fullRpakPath);
	if (dt)
	{
		dt->guid = guid;
		g_diskDatatables[guid] = dt;
		return 1;
	}
	return 0;
}

static int Datatable_ScanDirectory(const char* basePath, const char* subdir)
{
	int loaded = 0;
	char searchPath[DATATABLE_MAX_PATH_LEN * 2];
	V_snprintf(searchPath, sizeof(searchPath), "%s%s*.csv", basePath, subdir);

	FileFindHandle_t findHandle;
	const char* pFound = FileSystem()->FindFirstEx(searchPath, "GAME", &findHandle);

	while (pFound)
	{
		if (strlen(pFound) < DATATABLE_MAX_PATH_LEN)
		{
			char csvPath[DATATABLE_MAX_PATH_LEN * 2];
			V_snprintf(csvPath, sizeof(csvPath), "%s%s%s", basePath, subdir, pFound);
			loaded += Datatable_LoadSingleFile(csvPath, subdir, pFound);
		}
		pFound = FileSystem()->FindNext(findHandle);
	}

	FileSystem()->FindClose(findHandle);
	return loaded;
}

static void Datatable_LoadAllDiskFiles()
{
	if (!FileSystem())
		return;

	int loaded = 0;
	const char* basePath = "scripts/datatable/";

	loaded += Datatable_ScanDirectory(basePath, "");

	char searchPath[DATATABLE_MAX_PATH_LEN * 2];
	V_snprintf(searchPath, sizeof(searchPath), "%s*", basePath);

	FileFindHandle_t dirHandle;
	const char* pDir = FileSystem()->FindFirstEx(searchPath, "GAME", &dirHandle);

	while (pDir)
	{
		if (pDir[0] != '.' && strlen(pDir) < DATATABLE_MAX_PATH_LEN - 1)
		{
			char testPath[DATATABLE_MAX_PATH_LEN * 2];
			V_snprintf(testPath, sizeof(testPath), "%s%s/*.csv", basePath, pDir);

			FileFindHandle_t testHandle;
			if (FileSystem()->FindFirstEx(testPath, "GAME", &testHandle))
			{
				FileSystem()->FindClose(testHandle);

				char subdir[DATATABLE_MAX_PATH_LEN];
				V_snprintf(subdir, sizeof(subdir), "%s/", pDir);
				loaded += Datatable_ScanDirectory(basePath, subdir);
			}
		}
		pDir = FileSystem()->FindNext(dirHandle);
	}

	FileSystem()->FindClose(dirHandle);

	if (loaded > 0)
		Msg(eDLL_T::ENGINE, "[DiskDatatable] Loaded %d file(s) from disk\n", loaded);
}

static void Datatable_FreeAll()
{
	for (auto& pair : g_diskDatatables)
	{
		delete[] pair.second->memBlock;
		delete pair.second;
	}
	g_diskDatatables.clear();
}

//=============================================================================
// SCRIPT HOOK
//=============================================================================

static __int64 Hook_Script_GetDatatable(__int64 sqvm)
{
	if (!g_diskDatatablesScanned)
	{
		g_diskDatatablesScanned = true;
		Datatable_LoadAllDiskFiles();
	}

	if (!g_diskDatatables.empty())
	{
		__int64 stackTop = *(__int64*)(sqvm + 0x58);
		uint32_t typeTag = *(uint32_t*)(stackTop + 0x10);

		if (typeTag == 0x08000000)
		{
			__int64 stringHeader = *(__int64*)(stackTop + 0x18);
			if (stringHeader)
			{
				const char* assetPath = (const char*)(stringHeader + 0x40);

				if (assetPath && *assetPath)
				{
					uint64_t guid;
					if ((reinterpret_cast<uintptr_t>(assetPath) & 3) == 0)
						guid = v_HashNameAligned(assetPath);
					else
						guid = v_HashNameUnaligned(assetPath);

					auto it = g_diskDatatables.find(guid);
					if (it != g_diskDatatables.end())
					{
						DiskDatatable* dt = it->second;

						if (!dt->insertedInHashTable)
						{
							Datatable_InsertIntoAssetHashTable(guid, &dt->header);
							Datatable_InvalidateScriptCache();
							dt->insertedInHashTable = true;
						}
					}
				}
			}
		}
	}

	return v_Script_GetDatatable(sqvm);
}

//=============================================================================
// COMMANDS
//=============================================================================

static void CC_DatatableReload(const CCommand& args)
{
	for (auto& pair : g_diskDatatables)
		pair.second->insertedInHashTable = false;

	Datatable_FreeAll();
	Datatable_LoadAllDiskFiles();

	for (auto& pair : g_diskDatatables)
	{
		DiskDatatable* dt = pair.second;
		Datatable_InsertIntoAssetHashTable(dt->guid, &dt->header);
		dt->insertedInHashTable = true;
	}

	Datatable_InvalidateScriptCache();
	Msg(eDLL_T::ENGINE, "[DiskDatatable] Reloaded %d file(s)\n", static_cast<int>(g_diskDatatables.size()));
}

static ConCommand datatable_reload("datatable_reload", CC_DatatableReload,
	"Reload all disk datatables from CSV files", FCVAR_DEVELOPMENTONLY);

//=============================================================================
// DETOUR
//=============================================================================

void V_Datatable::Detour(const bool bAttach) const
{
	DetourSetup(&v_Script_GetDatatable, &Hook_Script_GetDatatable, bAttach);

	if (!bAttach)
	{
		Datatable_FreeAll();
		g_diskDatatablesScanned = false;
	}
}
