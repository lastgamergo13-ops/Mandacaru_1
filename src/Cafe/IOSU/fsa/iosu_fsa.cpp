#include "fsa_types.h"
#include "iosu_fsa.h"
#include "Cafe/IOSU/kernel/iosu_kernel.h"
#include "Cafe/Filesystem/fsc.h"
#include "util/helpers/helpers.h"

#include "Cafe/OS/libs/coreinit/coreinit_FS.h"	 // get rid of this dependency, requires reworking some of the IPC stuff. See locations where we use coreinit::FSCmdBlockBody_t
#include "Cafe/HW/Latte/Core/LatteBufferCache.h" // also remove this dependency

#include "Cafe/HW/MMU/MMU.h"
#include "Cemu/Logging/CemuLogging.h"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(__ANDROID__)
// DIAGNOSTICO: caminho de cada handle de arquivo aberto (para identificar os videos .bik nas leituras)
static std::mutex g_diagFsPathMutex;
static std::unordered_map<uint32, std::string> g_diagFsPaths;
#endif

using namespace iosu::kernel;

#if defined(__ANDROID__)
#include <thread>
#include <functional>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <atomic>
// DIAGNOSTICO (tela preta): resumo a cada 15 s das leituras de arquivo por caminho. Mostra se o jogo continua CARREGANDO (streaming do mundo,
// audio, videos) durante a tela preta ou se a leitura parou ("nenhuma leitura"). Uma thread leve imprime e zera os contadores.
struct AndroidFsDiagEntry { uint64 reads = 0, bytes = 0, shortReads = 0; uint32 lastPos = 0; };
static std::mutex g_androidFsDiagMutex;
static std::map<std::string, AndroidFsDiagEntry> g_androidFsDiag;
static std::atomic<bool> g_androidFsDiagThreadStarted{false};

static void AndroidFsDiagThreadMain()
{
	for (uint32 round = 1;; round++)
	{
		std::this_thread::sleep_for(std::chrono::seconds(15));
		std::map<std::string, AndroidFsDiagEntry> snapshot;
		{
			std::lock_guard<std::mutex> lk(g_androidFsDiagMutex);
			snapshot.swap(g_androidFsDiag);
		}
		if (snapshot.empty())
		{
			cemuLog_log(LogType::Force, "[FS] resumo #{} (15 s): NENHUMA leitura de arquivo (o jogo nao esta carregando nada do disco agora)", round);
			continue;
		}
		uint64 totalBytes = 0, totalReads = 0;
		std::string top;
		// os 6 arquivos com mais bytes
		std::multimap<uint64, std::string, std::greater<uint64>> byBytes;
		for (auto& [path, e] : snapshot)
		{
			totalBytes += e.bytes;
			totalReads += e.reads;
			byBytes.emplace(e.bytes, path);
		}
		int shown = 0;
		for (auto& [bytes, path] : byBytes)
		{
			if (shown++ >= 6)
				break;
			const AndroidFsDiagEntry& e = snapshot[path];
			const size_t slash = path.find_last_of('/');
			top += fmt::format(" | {} {} leituras {:.1f} MB{} ult.pos={:#x}", slash == std::string::npos ? path : path.substr(slash + 1), e.reads, bytes / 1048576.0,
				e.shortReads ? fmt::format(" ({} curtas)", e.shortReads) : std::string(), e.lastPos);
		}
		cemuLog_log(LogType::Force, "[FS] resumo #{} (15 s): {} leituras, {:.1f} MB em {} arquivo(s){}", round, totalReads, totalBytes / 1048576.0, snapshot.size(), top);
	}
}

static void AndroidFsDiagCount(const std::string& path, uint32 bytesRead, uint32 bytesRequested, uint32 pos)
{
	if (!g_androidFsDiagThreadStarted.exchange(true))
		std::thread(AndroidFsDiagThreadMain).detach();
	std::lock_guard<std::mutex> lk(g_androidFsDiagMutex);
	AndroidFsDiagEntry& e = g_androidFsDiag[path.empty() ? std::string("(sem nome)") : path];
	e.reads++;
	e.bytes += bytesRead;
	if (bytesRead != bytesRequested)
		e.shortReads++;
	e.lastPos = pos;
}
#endif

namespace iosu
{
	namespace fsa
	{
		IOSMsgQueueId sFSAIoMsgQueue;
		SysAllocator<iosu::kernel::IOSMessage, 352> _m_sFSAIoMsgQueueMsgBuffer;
		std::thread sFSAIoThread;

		struct FSAClient // IOSU's counterpart to the coreinit FSClient struct
		{
			std::string workingDirectory;
			bool isAllocated{false};

			void AllocateAndInitialize()
			{
				isAllocated = true;
				workingDirectory = std::string("/");
			}

			void ReleaseAndCleanup()
			{
				isAllocated = false;
			}
		};

		std::array<FSAClient, 624> sFSAClientArray;

		IOS_ERROR FSAAllocateClient(sint32& indexOut)
		{
			for (size_t i = 0; i < sFSAClientArray.size(); i++)
			{
				if (sFSAClientArray[i].isAllocated)
					continue;
				sFSAClientArray[i].AllocateAndInitialize();
				indexOut = (sint32)i;
				return IOS_ERROR_OK;
			}
			return (IOS_ERROR)0xFFFCFFEE;
		}

		FSA_RESULT FSA_convertFSCtoFSAStatus(sint32 fscError)
		{
			if (fscError == FSC_STATUS_OK)
				return FSA_RESULT::OK;
			else if (fscError == FSC_STATUS_FILE_NOT_FOUND)
				return FSA_RESULT::NOT_FOUND;
			else if (fscError == FSC_STATUS_ALREADY_EXISTS)
				return FSA_RESULT::ALREADY_EXISTS;
			cemu_assert_unimplemented();
			return FSA_RESULT::FATAL_ERROR;
		}

		std::string __FSATranslatePath(FSAClient* fsaClient, std::string_view input, bool endWithSlash = false)
		{
			std::string tmp;
			if (input.empty())
			{
				tmp.assign(fsaClient->workingDirectory);
				if (endWithSlash)
					tmp.push_back('/');
				return tmp;
			}
			char c = input.front();
			cemu_assert_debug(c != '\\'); // how to handle backward slashes?

			if (c == '/')
			{
				// absolute path
				tmp.assign("/"); // keep the leading slash
			}
			else if (c == '~')
			{
				cemu_assert_debug(false);
				input.remove_prefix(1);
				tmp.assign("/");
			}
			else
			{
				// in all other cases the path is relative to the working directory
				tmp.assign(fsaClient->workingDirectory);
			}
			// parse path
			size_t idx = 0;
			while (idx < input.size())
			{
				c = input[idx];
				if (c == '/')
				{
					idx++;
					continue; // filter leading and repeated slashes
				}

				if (c == '.')
				{
					if ((input.size() - idx) >= 3 && input[idx + 1] == '.' && input[idx + 2] == '/')
					{
						// "../"
                        while(!tmp.empty())
                        {
                            if(tmp.back() == '/')
                            {
                                tmp.pop_back();
                                break;
                            }
                            tmp.pop_back();
                        }
						idx += 3;
						continue;
					}
					else if ((input.size() - idx) == 2 && input[idx + 1] == '.')
					{
						// ".." at the end
                        while(!tmp.empty())
                        {
                            if(tmp.back() == '/')
                            {
                                tmp.pop_back();
                                break;
                            }
                            tmp.pop_back();
                        }
						idx += 2;
						continue;
					}
					else if ((input.size() - idx) >= 2 && input[idx + 1] == '/')
					{
						// "./"
						idx += 2;
						continue;
					}
				}
				if (!tmp.empty() && tmp.back() != '/')
					tmp.push_back('/');

				while (idx < input.size())
				{
					c = input[idx];
					if (c == '/')
						break;
					tmp.push_back(c);
					idx++;
				}
			}
			if (endWithSlash)
			{
				if (tmp.empty() || tmp.back() != '/')
					tmp.push_back('/');
			}
			return tmp;
		}

		FSCVirtualFile* __FSAOpenNode(FSAClient* client, std::string_view path, FSC_ACCESS_FLAG accessFlags, sint32& fscStatus)
		{
			std::string translatedPath = __FSATranslatePath(client, path);
			return fsc_open(translatedPath.c_str(), accessFlags, &fscStatus);
		}

		class _FSAHandleTable {
			struct _FSAHandleResource
			{
				bool isAllocated{false};
				FSCVirtualFile* fscFile;
				uint16 handleCheckValue;
			};

		public:
			FSA_RESULT AllocateHandle(FSResHandle& handleOut, FSCVirtualFile* fscFile)
			{
				for (size_t i = 0; i < m_handleTable.size(); i++)
				{
					auto& it = m_handleTable.at(i);
					if (it.isAllocated)
						continue;
					uint16 checkValue = (uint16)m_currentCounter;
					m_currentCounter++;
					it.handleCheckValue = checkValue;
					it.fscFile = fscFile;
					it.isAllocated = true;
					uint32 handleVal = ((uint32)i << 16) | (uint32)checkValue;
					handleOut = (FSResHandle)handleVal;
					return FSA_RESULT::OK;
				}
				cemuLog_log(LogType::Force, "FSA: Ran out of file handles");
				return FSA_RESULT::FATAL_ERROR;
			}

			FSA_RESULT ReleaseHandle(FSResHandle handle)
			{
				uint16 index = (uint16)((uint32)handle >> 16);
				uint16 checkValue = (uint16)(handle & 0xFFFF);
				if (index >= m_handleTable.size())
					return FSA_RESULT::INVALID_FILE_HANDLE;
				auto& it = m_handleTable.at(index);
				if (!it.isAllocated)
					return FSA_RESULT::INVALID_FILE_HANDLE;
				if (it.handleCheckValue != checkValue)
					return FSA_RESULT::INVALID_FILE_HANDLE;
				it.fscFile = nullptr;
				it.isAllocated = false;
				return FSA_RESULT::OK;
			}

			FSCVirtualFile* GetByHandle(FSResHandle handle)
			{
				uint16 index = (uint16)((uint32)handle >> 16);
				uint16 checkValue = (uint16)(handle & 0xFFFF);
				if (index >= m_handleTable.size())
					return nullptr;
				auto& it = m_handleTable.at(index);
				if (!it.isAllocated)
					return nullptr;
				if (it.handleCheckValue != checkValue)
					return nullptr;
				return it.fscFile;
			}

		private:
			uint32 m_currentCounter = 1;
			std::array<_FSAHandleResource, 0x3C0> m_handleTable;
		};

		_FSAHandleTable sFileHandleTable;
		_FSAHandleTable sDirHandleTable;

		FSA_RESULT __FSAOpenFile(FSAClient* client, const char* path, const char* accessModifierStr, sint32* fileHandle)
		{
			*fileHandle = FS_INVALID_HANDLE_VALUE;
			FSC_ACCESS_FLAG accessModifier = FSC_ACCESS_FLAG::NONE;
			bool truncateFile = false; // todo: Support for this
			bool isAppend = false;	   // todo: proper support for this (all write operations should move cursor to the end of the file?)
			if (strcmp(accessModifierStr, "r") == 0 || strcmp(accessModifierStr, "rb") == 0)
				accessModifier = FSC_ACCESS_FLAG::READ_PERMISSION;
			else if (strcmp(accessModifierStr, "r+") == 0)
			{
				// the cursor will be set to the beginning of the file
				// allows read and write access
				accessModifier = FSC_ACCESS_FLAG::READ_PERMISSION | FSC_ACCESS_FLAG::WRITE_PERMISSION; // read, write
			}
			else if (strcmp(accessModifierStr, "w") == 0)
			{
				accessModifier = FSC_ACCESS_FLAG::READ_PERMISSION | FSC_ACCESS_FLAG::WRITE_PERMISSION | FSC_ACCESS_FLAG::FILE_ALWAYS_CREATE; // create new file & write
				truncateFile = true;
			}
			else if (strcmp(accessModifierStr, "w+") == 0)
			{
				accessModifier = FSC_ACCESS_FLAG::READ_PERMISSION | FSC_ACCESS_FLAG::WRITE_PERMISSION | FSC_ACCESS_FLAG::FILE_ALWAYS_CREATE; // create new file & read & write
				truncateFile = true;
			}
			else if (strcmp(accessModifierStr, "wb") == 0) // used in Super Meat Boy
			{
				// b flag is allowed has no effect
				accessModifier = FSC_ACCESS_FLAG::READ_PERMISSION | FSC_ACCESS_FLAG::WRITE_PERMISSION | FSC_ACCESS_FLAG::FILE_ALWAYS_CREATE; // create new file & write
				truncateFile = true;
			}
			else if (strcmp(accessModifierStr, "a+") == 0)
			{
				accessModifier = FSC_ACCESS_FLAG::READ_PERMISSION | FSC_ACCESS_FLAG::WRITE_PERMISSION | FSC_ACCESS_FLAG::FILE_ALLOW_CREATE | FSC_ACCESS_FLAG::IS_APPEND;
				isAppend = true;
			}
			else if (strcmp(accessModifierStr, "a") == 0)
			{
				accessModifier = FSC_ACCESS_FLAG::WRITE_PERMISSION | FSC_ACCESS_FLAG::FILE_ALLOW_CREATE | FSC_ACCESS_FLAG::IS_APPEND;
				isAppend = true;
			}
			else
				cemu_assert_debug(false);

			accessModifier |= FSC_ACCESS_FLAG::OPEN_DIR | FSC_ACCESS_FLAG::OPEN_FILE;
			sint32 fscStatus;
			FSCVirtualFile* fscFile = __FSAOpenNode(client, path, accessModifier, fscStatus);
			if (!fscFile)
				return FSA_RESULT::NOT_FOUND;
			if (fscFile->fscGetType() != FSC_TYPE_FILE)
			{
				delete fscFile;
				return FSA_RESULT::NOT_FILE;
			}
			if (isAppend)
				fsc_setFileSeek(fscFile, fsc_getFileSize(fscFile));
			FSResHandle fsFileHandle;
			FSA_RESULT r = sFileHandleTable.AllocateHandle(fsFileHandle, fscFile);
			if (r != FSA_RESULT::OK)
			{
				cemuLog_log(LogType::Force, "Exceeded maximum number of FSA file handles");
				delete fscFile;
				return FSA_RESULT::MAX_FILES;
			}
			*fileHandle = fsFileHandle;
			cemuLog_log(LogType::CoreinitFile, "Open file {} (access: {} result: ok handle: 0x{})", path, accessModifierStr, (uint32)*fileHandle);
			return FSA_RESULT::OK;
		}

		FSA_RESULT __FSAOpenDirectory(FSAClient* client, std::string_view path, sint32* dirHandle)
		{
			*dirHandle = FS_INVALID_HANDLE_VALUE;
			sint32 fscStatus;
			FSCVirtualFile* fscFile = __FSAOpenNode(client, path, FSC_ACCESS_FLAG::OPEN_DIR | FSC_ACCESS_FLAG::OPEN_FILE, fscStatus);
			if (!fscFile)
				return FSA_RESULT::NOT_FOUND;
			if (fscFile->fscGetType() != FSC_TYPE_DIRECTORY)
			{
				delete fscFile;
				return FSA_RESULT::NOT_DIR;
			}
			FSResHandle fsDirHandle;
			FSA_RESULT r = sDirHandleTable.AllocateHandle(fsDirHandle, fscFile);
			if (r != FSA_RESULT::OK)
			{
				delete fscFile;
				return FSA_RESULT::MAX_DIRS;
			}
			*dirHandle = fsDirHandle;
			cemuLog_log(LogType::CoreinitFile, "Open directory {} (result: ok handle: 0x{})", path, (uint32)*dirHandle);
			return FSA_RESULT::OK;
		}

		FSA_RESULT __FSACloseFile(uint32 fileHandle)
		{
			uint8 handleType = 0;
			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
			{
				cemuLog_logDebug(LogType::Force, "__FSACloseFile(): Invalid handle (0x{:08x})", fileHandle);
				return FSA_RESULT::INVALID_FILE_HANDLE;
			}
			// unregister file
			sFileHandleTable.ReleaseHandle(fileHandle); // todo - use the error code of this
			fsc_close(fscFile);
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_remove(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			std::string path = __FSATranslatePath(client, (char*)shimBuffer->request.cmdRemove.path);
			sint32 fscStatus = FSC_STATUS_FILE_NOT_FOUND;
			fsc_remove(path.c_str(), &fscStatus);
			return FSA_convertFSCtoFSAStatus(fscStatus);
		}

		FSA_RESULT FSAProcessCmd_makeDir(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			std::string path = __FSATranslatePath(client, (char*)shimBuffer->request.cmdMakeDir.path);
			sint32 fscStatus = FSC_STATUS_FILE_NOT_FOUND;
			fsc_createDir(path.c_str(), &fscStatus);
			return FSA_convertFSCtoFSAStatus(fscStatus);
		}

		FSA_RESULT FSAProcessCmd_rename(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			std::string srcPath = __FSATranslatePath(client, (char*)shimBuffer->request.cmdRename.srcPath);
			std::string dstPath = __FSATranslatePath(client, (char*)shimBuffer->request.cmdRename.dstPath);
			sint32 fscStatus = FSC_STATUS_FILE_NOT_FOUND;
			fsc_rename(srcPath.c_str(), dstPath.c_str(), &fscStatus);
			return FSA_convertFSCtoFSAStatus(fscStatus);
		}

		bool __FSA_GetStatFromFSCFile(FSCVirtualFile* fscFile, FSStat_t* fsStatOut)
		{
			memset(fsStatOut, 0x00, sizeof(FSStat_t));
			FSFlag statFlag = FSFlag::NONE;
			if (fsc_isDirectory(fscFile))
			{
				// note: Only quota (save) directories have the size field set. For other directories it's zero.
				// Hyrule Warriors relies on the size field being zero for /vol/content/data/. Otherwise it will try to read it like a file and get stuck in an endless loop.
				// Art Academy reads the size for save directories
				statFlag |= FSFlag::IS_DIR;
				fsStatOut->size = 0;
			}
			else if (fsc_isFile(fscFile))
			{
				fsStatOut->size = fsc_getFileSize(fscFile);
                statFlag |= FSFlag::IS_FILE;
			}
			else
			{
				cemu_assert_suspicious();
			}
			fsStatOut->permissions = 0x777; // format matches unix (fstat) permissions?
			fsStatOut->flag = statFlag;
			return true;
		}

		FSA_RESULT __FSA_GetFileStat(FSAClient* client, const char* path, FSStat_t* fsStatOut)
		{
			sint32 fscStatus;
			FSCVirtualFile* fscFile = __FSAOpenNode(client, path, FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::OPEN_DIR, fscStatus);
			if (!fscFile)
				return FSA_convertFSCtoFSAStatus(fscStatus);
			__FSA_GetStatFromFSCFile(fscFile, fsStatOut);
			delete fscFile;
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_queryInfo(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			char* path = (char*)shimBuffer->request.cmdQueryInfo.query;
			uint32 queryType = shimBuffer->request.cmdQueryInfo.queryType;
			// handle query
			sint32 fscStatus = FSC_STATUS_OK;
			if (queryType == FSA_QUERY_TYPE_STAT)
			{
				FSStat_t* fsStat = &shimBuffer->response.cmdQueryInfo.queryStat.stat;
				FSA_RESULT fsaStatus = __FSA_GetFileStat(client, path, fsStat);
#if defined(__ANDROID__)
				{
					static std::atomic<uint32> s_statLogs{0};
					if (s_statLogs.fetch_add(1) < 300)
						cemuLog_log(LogType::Force, "[FS] consulta de info \"{}\" -> {} (FSA={:#x}) tamanho={} flag={:#x}", path, fsaStatus == FSA_RESULT::OK ? "OK" : "FALHOU", (uint32)(sint32)fsaStatus,
							fsaStatus == FSA_RESULT::OK ? (uint32)fsStat->size : 0u, fsaStatus == FSA_RESULT::OK ? (uint32)static_cast<uint32>(fsStat->flag.value()) : 0u);
				}
#endif
				return fsaStatus;
			}
			else if (queryType == FSA_QUERY_TYPE_FREESPACE)
			{
				sint32 fscStatus;
				FSCVirtualFile* fscFile = __FSAOpenNode(client, path, FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::OPEN_DIR, fscStatus);
				if (!fscFile)
					return FSA_convertFSCtoFSAStatus(fscStatus);
				betype<uint64>* fsStatSize = &shimBuffer->response.cmdQueryInfo.queryFreeSpace.freespace;
				*fsStatSize = 30ull * 1024 * 1024 * 1024; // placeholder value. How is this determined?
				delete fscFile;
				return FSA_RESULT::OK;
			}
			else if (queryType == FSA_QUERY_TYPE_DEVICE_INFO)
			{
				FSADeviceInfo_t* deviceInfo = &shimBuffer->response.cmdQueryInfo.queryDeviceInfo.info;
				// always report hardcoded values for now.
				deviceInfo->deviceSectorSize = 512;
				deviceInfo->deviceSizeInSectors = (32ull * 1024 * 1024 * 1024) / deviceInfo->deviceSectorSize;
				cemu_assert_suspicious();
				return FSA_RESULT::OK;
			}
			else
				cemu_assert_unimplemented();
			return FSA_convertFSCtoFSAStatus(fscStatus);
		}

		FSA_RESULT FSAProcessCmd_getStatFile(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			FSFileHandle2 fileHandle = shimBuffer->request.cmdGetStatFile.fileHandle;
			FSStat_t* statOut = &shimBuffer->response.cmdStatFile.statOut;
			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
				return FSA_RESULT::NOT_FOUND;
			cemu_assert_debug(fsc_isFile(fscFile));
			__FSA_GetStatFromFSCFile(fscFile, statOut);
#if defined(__ANDROID__)
			{
				static std::atomic<uint32> s_statFileLogs{0};
				if (s_statFileLogs.fetch_add(1) < 150)
					cemuLog_log(LogType::Force, "[FS] tamanho do arquivo aberto handle={}: {}", (uint32)fileHandle, (uint32)statOut->size);
			}
#endif
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_read(FSAClient* client, FSAShimBuffer* shimBuffer, MEMPTR<void> destPtr, uint32be transferSize)
		{
			uint32 transferElementSize = shimBuffer->request.cmdReadFile.size;
			uint32 filePos = shimBuffer->request.cmdReadFile.filePos;
			uint32 fileHandle = shimBuffer->request.cmdReadFile.fileHandle;
			uint32 flags = shimBuffer->request.cmdReadFile.flag;

			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_FILE_HANDLE;

			uint32 bytesToRead = transferSize;
			// update file position if flag is set
			if ((flags & FSA_CMD_FLAG_SET_POS) != 0)
				fsc_setFileSeek(fscFile, filePos);
			// todo: File permissions
			uint32 bytesSuccessfullyRead = fsc_readFile(fscFile, destPtr, bytesToRead);
#if defined(__ANDROID__)
			{
				// DIAGNOSTICO: leituras CURTAS (menos bytes do que o jogo pediu) podem deixar dados zerados na memoria do jogo e causar
				// ponteiros nulos mais tarde. Registra todas as leituras curtas (ate 200) e as primeiras 40 leituras normais.
				static std::atomic<uint32> s_shortCount{0}, s_normalCount{0};
				const bool isShort = bytesSuccessfullyRead != bytesToRead;
				if ((isShort ? s_shortCount.fetch_add(1) < 200 : s_normalCount.fetch_add(1) < 40))
					cemuLog_log(LogType::Force, "[FS] leitura handle={} pos={:#x} pedido={:#x} lido={:#x} destino={:08x}{}", fileHandle, (uint32)fsc_getFileSeek(fscFile) - bytesSuccessfullyRead, bytesToRead, bytesSuccessfullyRead, destPtr.GetMPTR(), isShort ? "  <-- LEITURA CURTA" : "");
			}
			{
				// DIAGNOSTICO: progresso de leitura dos videos (.bik). Mostra se o video continua sendo lido (tocando) ou parou.
				static std::atomic<uint32> s_bikLogCount{0};
				std::string path;
				{
					std::lock_guard<std::mutex> lk(g_diagFsPathMutex);
					auto it = g_diagFsPaths.find((uint32)fileHandle);
					if (it != g_diagFsPaths.end())
						path = it->second;
				}
				AndroidFsDiagCount(path, bytesSuccessfullyRead, bytesToRead, (uint32)fsc_getFileSeek(fscFile));
				if (path.size() > 4 && path.compare(path.size() - 4, 4, ".bik") == 0)
				{
					static std::mutex s_bikMutex;
					static std::unordered_map<uint32, uint64> s_bikBytes, s_bikLastLogged;
					std::lock_guard<std::mutex> lk(s_bikMutex);
					uint64& total = s_bikBytes[(uint32)fileHandle];
					total += bytesSuccessfullyRead;
					uint64& last = s_bikLastLogged[(uint32)fileHandle];
					if (total >= last + 0x200000 && s_bikLogCount.fetch_add(1) < 200)
					{
						last = total;
						cemuLog_log(LogType::Force, "[FS] video \"{}\" (handle {}): {:.1f} MB lidos ate agora, ultima leitura em pos={:#x}", path, fileHandle, total / 1048576.0, (uint32)fsc_getFileSeek(fscFile));
					}
				}
			}
#endif
			if (transferElementSize == 0)
				return FSA_RESULT::OK;

			LatteBufferCache_notifyDCFlush(destPtr.GetMPTR(), bytesToRead);

			return (FSA_RESULT)(bytesSuccessfullyRead / transferElementSize); // return number of elements read
		}

		FSA_RESULT FSAProcessCmd_write(FSAClient* client, FSAShimBuffer* shimBuffer, MEMPTR<void> destPtr, uint32be transferSize)
		{
			uint32 transferElementSize = shimBuffer->request.cmdWriteFile.size;
			uint32 filePos = shimBuffer->request.cmdWriteFile.filePos;
			uint32 fileHandle = shimBuffer->request.cmdWriteFile.fileHandle;
			uint32 flags = shimBuffer->request.cmdWriteFile.flag;

			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_FILE_HANDLE;
			cemu_assert_debug((transferSize % transferElementSize) == 0);
			uint32 bytesToWrite = transferSize;
			// check for write permission (should this happen before or after setting file position?)
			if (!fsc_isWritable(fscFile))
			{
				cemu_assert_debug(false);
				return FSA_RESULT::PERMISSION_ERROR;
			}
			// update file position if flag is set
			if ((flags & FSA_CMD_FLAG_SET_POS) != 0)
				fsc_setFileSeek(fscFile, filePos);
			uint32 bytesSuccessfullyWritten = fsc_writeFile(fscFile, destPtr, bytesToWrite);
			debug_printf("FSAProcessCmd_write(): Writing 0x%08x bytes (bytes actually written: 0x%08x)\n", bytesToWrite, bytesSuccessfullyWritten);
			return (FSA_RESULT)(bytesSuccessfullyWritten / transferElementSize); // return number of elements read
		}

		FSA_RESULT FSAProcessCmd_setPos(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			uint32 fileHandle = shimBuffer->request.cmdSetPosFile.fileHandle;
			uint32 filePos = shimBuffer->request.cmdSetPosFile.filePos;
			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_FILE_HANDLE;
			fsc_setFileSeek(fscFile, filePos);
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_getPos(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			uint32 fileHandle = shimBuffer->request.cmdGetPosFile.fileHandle;
			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_FILE_HANDLE;
			uint32 filePos = fsc_getFileSeek(fscFile);
			shimBuffer->response.cmdGetPosFile.filePos = filePos;
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_openFile(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			sint32 fileHandle = 0;
			FSA_RESULT fsaResult = __FSAOpenFile(client, (char*)shimBuffer->request.cmdOpenFile.path, (char*)shimBuffer->request.cmdOpenFile.mode, &fileHandle);
			shimBuffer->response.cmdOpenFile.fileHandleOutput = fileHandle;
#if defined(__ANDROID__)
			{
				// DIAGNOSTICO: o jogo abriu (ou tentou abrir) este arquivo. Falhas tem contador proprio para nunca serem cortadas pelas aberturas OK.
				static std::atomic<uint32> s_okCount{0}, s_failCount{0};
				const bool ok = (fsaResult == FSA_RESULT::OK);
				if (ok)
				{
					std::lock_guard<std::mutex> lk(g_diagFsPathMutex);
					g_diagFsPaths[(uint32)fileHandle] = std::string((const char*)shimBuffer->request.cmdOpenFile.path);
				}
				if ((ok ? s_okCount.fetch_add(1) < 300 : s_failCount.fetch_add(1) < 300))
					cemuLog_log(LogType::Force, "[FS] abrir arquivo \"{}\" modo=\"{}\" -> {} (FSA={:#x}, handle={})", (const char*)shimBuffer->request.cmdOpenFile.path, (const char*)shimBuffer->request.cmdOpenFile.mode, ok ? "OK" : "FALHOU", (uint32)(sint32)fsaResult, fileHandle);
			}
#endif
			return fsaResult;
		}

		FSA_RESULT FSAProcessCmd_closeFile(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			return __FSACloseFile(shimBuffer->request.cmdCloseFile.fileHandle);
		}

		FSA_RESULT FSAProcessCmd_openDir(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			sint32 dirHandle = 0;
			FSA_RESULT fsaResult = __FSAOpenDirectory(client, (const char*)shimBuffer->request.cmdOpenFile.path, &dirHandle);
			shimBuffer->response.cmdOpenDir.dirHandleOutput = dirHandle;
#if defined(__ANDROID__)
			{
				// DIAGNOSTICO: diretorios que o jogo lista (o texto que aparece nas tabelas do alocador sao NOMES de arquivos)
				static std::atomic<uint32> s_dirLogs{0};
				if (s_dirLogs.fetch_add(1) < 200)
					cemuLog_log(LogType::Force, "[FS] abrir diretorio \"{}\" -> {} (FSA={:#x}, handle={})", (const char*)shimBuffer->request.cmdOpenFile.path, fsaResult == FSA_RESULT::OK ? "OK" : "FALHOU", (uint32)(sint32)fsaResult, dirHandle);
			}
#endif
			return fsaResult;
		}

		FSA_RESULT FSAProcessCmd_readDir(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			FSCVirtualFile* fscFile = sDirHandleTable.GetByHandle((sint32)shimBuffer->request.cmdReadDir.dirHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_DIR_HANDLE;
			FSDirEntry_t* dirEntryOut = &shimBuffer->response.cmdReadDir.dirEntry;
			FSCDirEntry fscDirEntry;
			if (fsc_nextDir(fscFile, &fscDirEntry) == false)
				return FSA_RESULT::END_OF_DIRECTORY;
			strcpy(dirEntryOut->name, fscDirEntry.path);
			FSFlag statFlag = FSFlag::NONE;
			dirEntryOut->stat.size = 0;
			if (fscDirEntry.isDirectory)
			{
				statFlag |= FSFlag::IS_DIR;
			}
			else if (fscDirEntry.isFile)
			{
				statFlag |= FSFlag::IS_FILE;
				dirEntryOut->stat.size = fscDirEntry.fileSize;
			}
			dirEntryOut->stat.flag = statFlag;
			dirEntryOut->stat.permissions = 0x777;
#if defined(__ANDROID__)
			{
				static std::atomic<uint32> s_entryLogs{0};
				if (s_entryLogs.fetch_add(1) < 500)
					cemuLog_log(LogType::Force, "[FS] entrada de diretorio handle={}: \"{}\" {} tamanho={}", (sint32)shimBuffer->request.cmdReadDir.dirHandle, dirEntryOut->name, fscDirEntry.isDirectory ? "(pasta)" : "(arquivo)", (uint32)fscDirEntry.fileSize);
			}
#endif
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_closeDir(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			FSCVirtualFile* fscFile = sDirHandleTable.GetByHandle((sint32)shimBuffer->request.cmdReadDir.dirHandle);
			if (!fscFile)
			{
				cemuLog_logDebug(LogType::Force, "CloseDir: Invalid handle (0x{:08x})", (sint32)shimBuffer->request.cmdReadDir.dirHandle);
				return FSA_RESULT::INVALID_DIR_HANDLE;
			}
			sDirHandleTable.ReleaseHandle(shimBuffer->request.cmdReadDir.dirHandle);
			fsc_close(fscFile);
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_flushQuota(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_rewindDir(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			FSCVirtualFile* fscFile = sDirHandleTable.GetByHandle((sint32)shimBuffer->request.cmdRewindDir.dirHandle);
			if (!fscFile)
			{
				cemuLog_logDebug(LogType::Force, "RewindDir: Invalid handle (0x{:08x})", (sint32)shimBuffer->request.cmdRewindDir.dirHandle);
				return FSA_RESULT::INVALID_DIR_HANDLE;
			}
			if (!fscFile->fscRewindDir())
				return FSA_RESULT::FATAL_ERROR;

			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_flushFile(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_appendFile(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(shimBuffer->request.cmdAppendFile.fileHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_FILE_HANDLE;
#ifdef CEMU_DEBUG_ASSERT
			cemuLog_log(LogType::Force, "FSAProcessCmd_appendFile(): size 0x{:08x} count 0x{:08x} (todo)\n", shimBuffer->request.cmdAppendFile.size, shimBuffer->request.cmdAppendFile.count);
#endif
			return (FSA_RESULT)(shimBuffer->request.cmdAppendFile.count.value());
		}

		FSA_RESULT FSAProcessCmd_truncateFile(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			FSFileHandle2 fileHandle = shimBuffer->request.cmdTruncateFile.fileHandle;
			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_FILE_HANDLE;
			fsc_setFileLength(fscFile, fsc_getFileSeek(fscFile));
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_isEof(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			uint32 fileHandle = shimBuffer->request.cmdIsEof.fileHandle;
			FSCVirtualFile* fscFile = sFileHandleTable.GetByHandle(fileHandle);
			if (!fscFile)
				return FSA_RESULT::INVALID_FILE_HANDLE;
			uint32 filePos = fsc_getFileSeek(fscFile);
			uint32 fileSize = fsc_getFileSize(fscFile);
			if (filePos >= fileSize)
				return FSA_RESULT::END_OF_FILE;
			return FSA_RESULT::OK;
		}

		FSA_RESULT FSAProcessCmd_getCwd(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			char* pathOutput = shimBuffer->response.cmdGetCWD.path;
			sint32 pathOutputMaxLen = sizeof(shimBuffer->response.cmdGetCWD.path);
			cemu_assert(pathOutputMaxLen > 0);
			sint32 fscStatus = FSC_STATUS_OK;
			strncpy(pathOutput, client->workingDirectory.data(), std::min(client->workingDirectory.size() + 1, (size_t)pathOutputMaxLen));
			pathOutput[pathOutputMaxLen - 1] = '\0';
			return FSA_convertFSCtoFSAStatus(fscStatus);
		}

		FSA_RESULT FSAProcessCmd_changeDir(FSAClient* client, FSAShimBuffer* shimBuffer)
		{
			const char* path = (const char*)shimBuffer->request.cmdChangeDir.path;
			shimBuffer->request.cmdChangeDir.path[sizeof(shimBuffer->request.cmdChangeDir.path) - 1] = '\0';
			sint32 fscStatus = FSC_STATUS_OK;
			client->workingDirectory.assign(__FSATranslatePath(client, path, true));
			return FSA_convertFSCtoFSAStatus(fscStatus);
		}

		void FSAHandleCommandIoctlv(FSAClient* client, IPCCommandBody* cmd, FSA_CMD_OPERATION_TYPE operationId, uint32 numIn, uint32 numOut, IPCIoctlVector* vec)
		{
			FSA_RESULT fsaResult = FSA_RESULT::FATAL_ERROR;

			switch (operationId)
			{
			case FSA_CMD_OPERATION_TYPE::READ:
			{
				fsaResult = FSAProcessCmd_read(client, (FSAShimBuffer*)vec[0].basePhys.GetPtr(), vec[1].basePhys, vec[1].size);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::WRITE:
			{
				fsaResult = FSAProcessCmd_write(client, (FSAShimBuffer*)vec[0].basePhys.GetPtr(), vec[1].basePhys, vec[1].size);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::CHANGEDIR:
			case FSA_CMD_OPERATION_TYPE::GETCWD:
			case FSA_CMD_OPERATION_TYPE::MAKEDIR:
			case FSA_CMD_OPERATION_TYPE::RENAME:
			case FSA_CMD_OPERATION_TYPE::OPENDIR:
			case FSA_CMD_OPERATION_TYPE::READDIR:
			case FSA_CMD_OPERATION_TYPE::CLOSEDIR:
			case FSA_CMD_OPERATION_TYPE::OPENFILE:
			case FSA_CMD_OPERATION_TYPE::REMOVE:
			case FSA_CMD_OPERATION_TYPE::GETPOS:
			case FSA_CMD_OPERATION_TYPE::SETPOS:
			case FSA_CMD_OPERATION_TYPE::ISEOF:
			case FSA_CMD_OPERATION_TYPE::GETSTATFILE:
			case FSA_CMD_OPERATION_TYPE::CLOSEFILE:
			case FSA_CMD_OPERATION_TYPE::QUERYINFO:
			case FSA_CMD_OPERATION_TYPE::APPENDFILE:
			case FSA_CMD_OPERATION_TYPE::TRUNCATEFILE:
			case FSA_CMD_OPERATION_TYPE::FLUSHQUOTA:
			{
				// These are IOCTL and no IOCTLV
				cemu_assert_error();
				break;
			}
			default:
			{
				cemu_assert_unimplemented();
				break;
			}
			}

			IOS_ResourceReply(cmd, (IOS_ERROR)fsaResult);
		}

		void FSAHandleCommandIoctl(FSAClient* client, IPCCommandBody* cmd, FSA_CMD_OPERATION_TYPE operationId, void* ptrIn, void* ptrOut)
		{
			FSAShimBuffer* shimBuffer = (FSAShimBuffer*)ptrIn;
			FSA_RESULT fsaResult = FSA_RESULT::FATAL_ERROR;

			switch (operationId)
			{
			case FSA_CMD_OPERATION_TYPE::REMOVE:
			{
				fsaResult = FSAProcessCmd_remove(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::CHANGEDIR:
			{
				fsaResult = FSAProcessCmd_changeDir(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::GETCWD:
			{
				fsaResult = FSAProcessCmd_getCwd(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::MAKEDIR:
			{
				fsaResult = FSAProcessCmd_makeDir(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::RENAME:
			{
				fsaResult = FSAProcessCmd_rename(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::OPENDIR:
			{
				fsaResult = FSAProcessCmd_openDir(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::READDIR:
			{
				fsaResult = FSAProcessCmd_readDir(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::CLOSEDIR:
			{
				fsaResult = FSAProcessCmd_closeDir(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::OPENFILE:
			{
				fsaResult = FSAProcessCmd_openFile(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::GETPOS:
			{
				fsaResult = FSAProcessCmd_getPos(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::SETPOS:
			{
				fsaResult = FSAProcessCmd_setPos(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::ISEOF:
			{
				fsaResult = FSAProcessCmd_isEof(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::GETSTATFILE:
			{
				fsaResult = FSAProcessCmd_getStatFile(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::CLOSEFILE:
			{
				fsaResult = FSAProcessCmd_closeFile(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::QUERYINFO:
			{
				fsaResult = FSAProcessCmd_queryInfo(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::APPENDFILE:
			{
				fsaResult = FSAProcessCmd_appendFile(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::TRUNCATEFILE:
			{
				fsaResult = FSAProcessCmd_truncateFile(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::FLUSHQUOTA:
			{
				fsaResult = FSAProcessCmd_flushQuota(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::REWINDDIR:
			{
				fsaResult = FSAProcessCmd_rewindDir(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::FLUSHFILE:
			{
				fsaResult = FSAProcessCmd_flushFile(client, shimBuffer);
				break;
			}
			case FSA_CMD_OPERATION_TYPE::READ:
			case FSA_CMD_OPERATION_TYPE::WRITE:
			{
				// These commands are IOCTLVs not IOCTL
				cemu_assert_error();
			}
			}
			IOS_ResourceReply(cmd, (IOS_ERROR)fsaResult);
		}

		void FSAIoThread()
		{
			SetThreadName("IOSU-FSA");
			IOSMessage msg;
			while (true)
			{
				IOS_ERROR r = IOS_ReceiveMessage(sFSAIoMsgQueue, &msg, 0);
				cemu_assert(!IOS_ResultIsError(r));
				if (msg == 0)
					return; // shutdown signaled
				IPCCommandBody* cmd = MEMPTR<IPCCommandBody>(msg).GetPtr();
				uint32 clientHandle = (uint32)cmd->devHandle;
				if (cmd->cmdId == IPCCommandId::IOS_OPEN)
				{
					sint32 clientIndex = 0;
					r = FSAAllocateClient(clientIndex);
					if (r != IOS_ERROR_OK)
					{
						IOS_ResourceReply(cmd, r);
						continue;
					}
					IOS_ResourceReply(cmd, (IOS_ERROR)clientIndex);
					continue;
				}
				else if (cmd->cmdId == IPCCommandId::IOS_CLOSE)
				{
					cemu_assert(clientHandle < sFSAClientArray.size());
					sFSAClientArray[clientHandle].ReleaseAndCleanup();
					IOS_ResourceReply(cmd, IOS_ERROR_OK);
					continue;
				}
				else if (cmd->cmdId == IPCCommandId::IOS_IOCTL)
				{
					cemu_assert(clientHandle < sFSAClientArray.size());
					cemu_assert(sFSAClientArray[clientHandle].isAllocated);
					FSAHandleCommandIoctl(sFSAClientArray.data() + clientHandle, cmd, (FSA_CMD_OPERATION_TYPE)cmd->args[0].value(), MEMPTR<void>(cmd->args[1]), MEMPTR<void>(cmd->args[3]));
				}
				else if (cmd->cmdId == IPCCommandId::IOS_IOCTLV)
				{
					cemu_assert(clientHandle < sFSAClientArray.size());
					cemu_assert(sFSAClientArray[clientHandle].isAllocated);
					FSA_CMD_OPERATION_TYPE requestId = (FSA_CMD_OPERATION_TYPE)cmd->args[0].value();
					uint32 numIn = cmd->args[1];
					uint32 numOut = cmd->args[2];
					IPCIoctlVector* vec = MEMPTR<IPCIoctlVector>{cmd->args[3]}.GetPtr();
					FSAHandleCommandIoctlv(sFSAClientArray.data() + clientHandle, cmd, requestId, numIn, numOut, vec);
				}
				else
				{
					cemuLog_log(LogType::Force, "/dev/fsa: Unsupported IPC cmdId");
					cemu_assert_suspicious();
					IOS_ResourceReply(cmd, IOS_ERROR_INVALID);
				}
			}
		}

		void Initialize()
		{
			for (auto& it : sFSAClientArray)
				it.ReleaseAndCleanup();
			sFSAIoMsgQueue = (IOSMsgQueueId)IOS_CreateMessageQueue(_m_sFSAIoMsgQueueMsgBuffer.GetPtr(), _m_sFSAIoMsgQueueMsgBuffer.GetCount());
			IOS_ERROR r = IOS_RegisterResourceManager("/dev/fsa", sFSAIoMsgQueue);
			IOS_DeviceAssociateId("/dev/fsa", 11);
			cemu_assert(!IOS_ResultIsError(r));
			sFSAIoThread = std::thread(FSAIoThread);
		}

		void Shutdown()
		{
			IOS_SendMessage(sFSAIoMsgQueue, 0, 0);
			sFSAIoThread.join();
		}
	} // namespace fsa
} // namespace iosu
