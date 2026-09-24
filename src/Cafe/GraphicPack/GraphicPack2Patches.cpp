#include <algorithm>

#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Common/FileStream.h"
#include "WindowSystem.h"
#include "util/helpers/StringParser.h"
#include "Cemu/PPCAssembler/ppcAssembler.h"
#include "Cafe/OS/RPL/rpl_structs.h"
#include "boost/algorithm/string.hpp"

// error handler
void PatchErrorHandler::printError(class PatchGroup* patchGroup, sint32 lineNumber, std::string_view errorMsg)
{
	if (m_anyErrorTriggered == false)
	{
		// stage error msg
		cemu_assert(m_gp);
		if (m_stage == STAGE::PARSER)
			cemuLog_writeLineToLog(fmt::format("An error occurred while trying to parse the patches for graphic pack \'{}\'", m_gp->GetName()), true, true);
		else if (m_stage == STAGE::APPLY)
			cemuLog_writeLineToLog(fmt::format("An error occurred while trying to apply the patches for graphic pack \'{}\'", m_gp->GetName()), true, true);
	}

	std::string msg;
	if (patchGroup == nullptr && lineNumber >= 0)
		msg.append(fmt::format("[Line {}] ", lineNumber));
	else if (patchGroup && lineNumber >= 0)
		msg.append(fmt::format("[{}, Line {}] ", patchGroup->getName(), lineNumber));
	else if (patchGroup && lineNumber < 0)
		msg.append(fmt::format("[{}] ", patchGroup->getName()));

	msg.append(errorMsg);

	cemuLog_writeLineToLog(msg, true, true);
	m_anyErrorTriggered = true;

	if (cemuLog_isLoggingEnabled(LogType::Patches))
		errorMessages.emplace_back(msg);
}

void PatchErrorHandler::showStageErrorMessageBox()
{
	std::string errorMsg;
	if (m_gp)
	{
		if (m_stage == STAGE::PARSER)
			errorMsg.assign(_tr("Failed to load patches for graphic pack \'{}\'", m_gp->GetName()));
		else
			errorMsg.assign(_tr("Failed to apply patches for graphic pack \'{}\'", m_gp->GetName()));
	}
	else
	{
		cemu_assert_debug(false); // graphic pack should always be set
	}
	if (cemuLog_isLoggingEnabled(LogType::Patches))
	{
		errorMsg.append("\n \n")
			.append(_tr("Details:"))
			.append("\n");
		for (auto& itr : errorMessages)
		{
			errorMsg.append(itr);
			errorMsg.append("\n");
		}
	}

	WindowSystem::ShowErrorDialog(errorMsg, _tr("Graphic pack error"), WindowSystem::ErrorCategory::GRAPHIC_PACKS);
}

// loads Cemu-style patches (patch_<anything>.asm)
// returns true if at least one file was found even if it could not be successfully parsed
bool GraphicPack2::LoadCemuPatches()
{
	bool foundPatches = false;
	fs::path path(m_rulesPath);
	path.remove_filename();
	for (auto& p : fs::directory_iterator(path))
	{
		auto& path = p.path();
		if (fs::is_regular_file(p.status()) && path.has_filename())
		{
			// check if filename matches
			std::string filename = _pathToUtf8(path.filename());
			if (boost::istarts_with(filename, "patch_") && boost::iends_with(filename, ".asm"))
			{
				FileStream* patchFile = FileStream::openFile2(path);
				if (patchFile)
				{
					// read file
					std::vector<uint8> fileData;
					patchFile->extract(fileData);
					delete patchFile;
					MemStreamReader patchesStream(fileData.data(), (sint32)fileData.size());
					// load Cemu style patch file
					if (!ParseCemuPatchesTxtInternal(patchesStream))
					{
						cemuLog_log(LogType::Force, "Error while processing \"{}\". No patches for this graphic pack will be applied.", _pathToUtf8(path));
						cemu_assert_debug(list_patchGroups.empty());
						return true; // return true since a .asm patch was found even if we could not parse it
					}
				}
				else
				{
					cemuLog_log(LogType::Force, "Unable to load patch file \"{}\"", _pathToUtf8(path));
				}
				foundPatches = true;
			}
		}
	}
	return foundPatches;
}

void GraphicPack2::LoadPatchFiles()
{
	// order of loading patches:
	// 1) Load Cemu-style patches (patch_<name>.asm), stop here if at least one patch file exists
	// 2) Load Cemuhook patches.txt
	if (LoadCemuPatches())
		return; // exit if at least one Cemu style patch file was found
	// fall back to Cemuhook patches.txt to guarantee backward compatibility
	fs::path path(m_rulesPath);
	path.remove_filename();
	path.append("patches.txt");
	FileStream* patchFile = FileStream::openFile2(path);
	if (patchFile == nullptr)
		return;
	// read file
	std::vector<uint8> fileData;
	patchFile->extract(fileData);
	delete patchFile;
	cemu_assert_debug(list_patchGroups.empty());
	// parse
	MemStreamReader patchesStream(fileData.data(), (sint32)fileData.size());
	ParseCemuhookPatchesTxtInternal(patchesStream);
}

void GraphicPack2::EnablePatches()
{
	std::lock_guard<std::recursive_mutex> lock(mtx_patches);
	for (auto& itr : list_modules)
		ApplyPatchesForModule(itr);
}

void GraphicPack2::UnloadPatches()
{
	if (list_patchGroups.empty())
		return;
	std::lock_guard<std::recursive_mutex> lock(mtx_patches);
	// if any patch groups were applied then revert here
	// do this by calling RevertPatchesForModule for every module?
	for (auto& itr : list_modules)
		RevertPatchesForModule(itr);
	// delete all patches
	for (auto itr : list_patchGroups)
		delete itr;
	list_patchGroups.clear();
}

bool GraphicPack2::HasPatches()
{
	return !list_patchGroups.empty();
}

const std::vector<PatchGroup*>& GraphicPack2::GetPatchGroups() {
	return list_patchGroups;
}

void GraphicPack2::ApplyPatchesForModule(const RPLModule* rpl)
{
	if (list_patchGroups.empty())
		return;
	// gather list of all patch groups that apply to this module
	std::vector<PatchGroup*> list_groups;
	for (auto itr : list_patchGroups)
	{
		if (itr->matchesCRC(rpl->patchCRC) || (itr->m_isRpxOnlyTarget && rpl->IsRPX()))
			list_groups.emplace_back(itr);
	}
	// apply all groups at once
	if (!list_groups.empty())
		ApplyPatchGroups(list_groups, rpl);
}

void GraphicPack2::RevertPatchesForModule(const RPLModule* rpl)
{
	if (list_patchGroups.empty())
		return;
	// gather list of all patch groups that apply to this module
	std::vector<PatchGroup*> list_groups;
	for (auto itr : list_patchGroups)
	{
		if (itr->matchesCRC(rpl->patchCRC) || (itr->m_isRpxOnlyTarget && rpl->IsRPX()))
			list_groups.emplace_back(itr);
	}
	// undo all groups at once
	if (!list_groups.empty())
		UndoPatchGroups(list_groups, rpl);
}

PatchGroup* GraphicPack2::FindPatchGroupDefiningSymbol(const std::string& symbolName)
{
	for (auto candidate : list_patchGroups)
	{
		for (auto patch : candidate->list_patches)
		{
			if (auto label = dynamic_cast<PatchEntryLabel*>(patch); label && label->getSymbolName() == symbolName)
				return candidate;
			if (auto symbolValue = dynamic_cast<PatchEntryCemuhookSymbolValue*>(patch); symbolValue && symbolValue->getSymbolName() == symbolName)
				return candidate;
			if (auto variableValue = dynamic_cast<PatchEntryVariableValue*>(patch); variableValue && variableValue->getSymbolName() == symbolName)
				return candidate;
		}
	}
	return nullptr;
}

bool GraphicPack2::RevertPatchGroup(PatchGroup* group)
{
	std::lock_guard<std::recursive_mutex> lock(mtx_patches);
	if (!group->isApplied())
		return true;
	for (auto rpl : list_modules)
	{
		if (group->matchesCRC(rpl->patchCRC) || (group->m_isRpxOnlyTarget && rpl->IsRPX()))
		{
			std::vector<PatchGroup*> single{ group };
			UndoPatchGroups(single, rpl);
		}
	}
	return !group->isApplied();
}

bool GraphicPack2::ApplyPatchGroupWithDependencies(PatchGroup* group)
{
	std::lock_guard<std::recursive_mutex> lock(mtx_patches);
	if (group->isApplied())
		return true;

	std::vector<PatchGroup*> batch{ group };
	std::vector<PatchGroup*> autoIncluded;

	// Try applying the group (plus, on each retry, whatever dependency groups the
	// previous attempt's unresolved-symbol errors pointed us to). Bounded to a handful
	// of rounds so a genuinely unsatisfiable or cyclic dependency can't loop forever -
	// in practice a single round covers the common "this cheat needs a helper value
	// defined by another cheat in the same pack" case.
	for (sint32 round = 0; round < 5; round++)
	{
		std::vector<std::string> unresolvedSymbols;
		bool triedAnyModule = false;
		for (auto rpl : list_modules)
		{
			bool matches = false;
			for (auto g : batch)
			{
				if (g->matchesCRC(rpl->patchCRC) || (g->m_isRpxOnlyTarget && rpl->IsRPX()))
				{
					matches = true;
					break;
				}
			}
			if (!matches)
				continue;
			triedAnyModule = true;
			std::vector<PatchGroup*> notYetApplied;
			for (auto g : batch)
				if (!g->isApplied())
					notYetApplied.push_back(g);
			if (!notYetApplied.empty())
				ApplyPatchGroups(notYetApplied, rpl, &unresolvedSymbols);
		}
		if (!triedAnyModule || group->isApplied())
			break;

		bool addedNewDependency = false;
		for (auto& symbolName : unresolvedSymbols)
		{
			PatchGroup* dependency = FindPatchGroupDefiningSymbol(symbolName);
			if (dependency && dependency != group && std::find(batch.begin(), batch.end(), dependency) == batch.end())
			{
				batch.push_back(dependency);
				autoIncluded.push_back(dependency);
				addedNewDependency = true;
			}
		}
		if (!addedNewDependency)
			break; // nothing more we can discover - give up cleanly
	}

	if (!group->isApplied())
	{
		// didn't work out - revert any dependency groups we ended up turning on along
		// the way too, so a cheat that ultimately failed doesn't silently leave
		// unrelated ones active behind it
		for (auto dep : autoIncluded)
			RevertPatchGroup(dep);
		return false;
	}

	m_userRequestedGroups.insert(group);
	if (!autoIncluded.empty())
	{
		m_groupDependencies[group] = autoIncluded;
		for (auto dep : autoIncluded)
			m_dependencyRefCount[dep]++;
	}
	return true;
}

bool GraphicPack2::RevertPatchGroupAndOrphanedDependencies(PatchGroup* group)
{
	std::lock_guard<std::recursive_mutex> lock(mtx_patches);
	bool ok = RevertPatchGroup(group);
	m_userRequestedGroups.erase(group);

	auto it = m_groupDependencies.find(group);
	if (it != m_groupDependencies.end())
	{
		for (auto dependency : it->second)
		{
			auto refCountIt = m_dependencyRefCount.find(dependency);
			if (refCountIt == m_dependencyRefCount.end())
				continue;
			refCountIt->second--;
			if (refCountIt->second <= 0 && !m_userRequestedGroups.contains(dependency))
			{
				// nobody else needs this dependency active anymore, and the user never
				// asked for it directly either - safe to turn it back off too
				RevertPatchGroup(dependency);
				m_dependencyRefCount.erase(refCountIt);
			}
		}
		m_groupDependencies.erase(it);
	}
	return ok;
}

std::recursive_mutex GraphicPack2::mtx_patches;
std::vector<const RPLModule*> GraphicPack2::list_modules;
