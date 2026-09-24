#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/CemuConfig.h"
#include "JNIUtils.h"

/*
 * In-game Cheats manager: exposes the cheats (patch groups, see GraphicPack2Patches.h)
 * belonging to whichever graphic packs are currently active for the running game.
 * This is the same underlying mechanism Cemu already uses for graphic pack "patches" -
 * nothing new was invented at the file-format or parsing level, this only adds a way to
 * toggle a single one of them independently and live (see
 * GraphicPack2::ApplyPatchGroupWithDependencies / RevertPatchGroupAndOrphanedDependencies
 * in GraphicPack2Patches.cpp for that part).
 *
 * Deliberately does NOT take a titleId parameter: GraphicPack2::GetActiveGraphicPacks()
 * already only contains packs enabled for and loaded with the currently running game -
 * there is nothing further to filter by here.
 *
 * Scoped to cheats only, not visual/texture mods: a whole graphic pack is only enabled for
 * real the NEXT time a game is launched (a saved preference, applied on boot) - toggling one
 * live mid-game (shaders, texture rules, upscale filters) has no concurrency lock protecting
 * it against the render thread, unlike patches which are already protected by mtx_patches.
 * Live-toggling mods would need that lock added on the Cemu side first; out of scope here.
 */

extern "C" [[maybe_unused]] JNIEXPORT jobjectArray JNICALL
Java_info_cemu_cemu_nativeinterface_NativeCheats_getCheatsForCurrentGame([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	struct CheatRef
	{
		GraphicPack2* pack;
		PatchGroup* group;
	};
	std::vector<CheatRef> cheats;
	for (auto& pack : GraphicPack2::GetActiveGraphicPacks())
	{
		for (auto group : pack->GetPatchGroups())
			cheats.push_back({pack.get(), group});
	}

	jclass cheatInfoClass = env->FindClass("info/cemu/cemu/nativeinterface/NativeCheats$CheatInfo");
	jmethodID cheatInfoCtorId = env->GetMethodID(cheatInfoClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;Z)V");

	return JNIUtils::CreateObjectArray(
		env,
		cheatInfoClass,
		cheats,
		[&](const CheatRef& cheat) {
			jstring packName = JNIUtils::ToJString(env, cheat.pack->GetName());
			jstring cheatName = JNIUtils::ToJString(env, cheat.group->getName());
			return env->NewObject(cheatInfoClass, cheatInfoCtorId, packName, cheatName, cheat.group->isApplied());
		});
}

namespace
{
	// Re-resolves (packName, cheatName) back to the live pack/group pointers - avoids
	// handing out raw native pointers across the JNI boundary, at the cost of a linear
	// scan over what is in practice always a short list.
	bool FindCheat(const std::string& packName, const std::string& cheatName, GraphicPack2** outPack, PatchGroup** outGroup)
	{
		for (auto& pack : GraphicPack2::GetActiveGraphicPacks())
		{
			if (pack->GetName() != packName)
				continue;
			for (auto group : pack->GetPatchGroups())
			{
				if (group->getName() == cheatName)
				{
					*outPack = pack.get();
					*outGroup = group;
					return true;
				}
			}
		}
		return false;
	}
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeCheats_setCheatEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jstring packName, jstring cheatName, jboolean enabled)
{
	GraphicPack2* pack = nullptr;
	PatchGroup* group = nullptr;
	if (!FindCheat(JNIUtils::FromJString(env, packName), JNIUtils::FromJString(env, cheatName), &pack, &group))
		return JNI_FALSE;
	if (enabled)
		return pack->ApplyPatchGroupWithDependencies(group) ? JNI_TRUE : JNI_FALSE;
	return pack->RevertPatchGroupAndOrphanedDependencies(group) ? JNI_TRUE : JNI_FALSE;
}

// Quality of Life master switch: whether the "Cheats" in-game menu button (see
// EmulationScreen.kt) shows itself at all. Off by default, like the rest of Experimental/Hackers.
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeCheats_isInGameCheatsEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return GetConfig().qol_in_game_cheats_enabled;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeCheats_setInGameCheatsEnabled([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean enabled)
{
	GetConfig().qol_in_game_cheats_enabled = static_cast<bool>(enabled);
}
