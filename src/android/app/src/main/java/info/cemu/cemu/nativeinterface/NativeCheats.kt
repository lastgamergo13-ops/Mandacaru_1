package info.cemu.cemu.nativeinterface

import androidx.annotation.Keep

object NativeCheats {
    /**
     * One cheat (a Graphic Pack "patch group") belonging to a currently active graphic pack.
     * [packName] is which pack it comes from - shown so cheats from different packs aren't
     * confused with each other when several packs are active at once. [enabled] mirrors the
     * live, current state, refreshed by calling getCheatsForCurrentGame() again after every
     * toggle (auto-included dependency cheats can flip on/off on their own - see
     * GraphicPack2::ApplyPatchGroupWithDependencies in NativeCheats.cpp).
     */
    @Keep
    data class CheatInfo(
        val packName: String,
        val cheatName: String,
        val enabled: Boolean,
    )

    @JvmStatic
    external fun getCheatsForCurrentGame(): Array<CheatInfo>

    @JvmStatic
    external fun setCheatEnabled(packName: String, cheatName: String, enabled: Boolean): Boolean

    // Quality of Life master switch: whether the "Cheats" in-game menu button shows itself at
    // all. Off by default, like the rest of Experimental/Hackers.
    @JvmStatic
    external fun isInGameCheatsEnabled(): Boolean

    @JvmStatic
    external fun setInGameCheatsEnabled(enabled: Boolean)
}
