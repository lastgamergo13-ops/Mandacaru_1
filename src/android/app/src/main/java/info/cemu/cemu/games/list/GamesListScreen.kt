@file:OptIn(
    ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class,
)

package info.cemu.cemu.games.list

import android.app.Activity
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.pulltorefresh.PullToRefreshDefaults
import androidx.compose.material3.pulltorefresh.pullToRefresh
import androidx.compose.material3.pulltorefresh.rememberPullToRefreshState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.components.AppSidebar
import info.cemu.cemu.common.ui.components.SidebarDestination
import info.cemu.cemu.common.ui.components.SidebarToggleButton
import info.cemu.cemu.common.ui.components.rememberDefaultSidebarActions
import info.cemu.cemu.common.ui.extensions.showMessage
import info.cemu.cemu.common.cemufiles.logFileExists
import info.cemu.cemu.common.cemufiles.tryOpenCemuFolder
import info.cemu.cemu.common.cemufiles.tryShareLogFile
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.common.ui.theme.SteamDeckColors
import info.cemu.cemu.nativeinterface.NativeGameTitles
import info.cemu.cemu.nativeinterface.NativeGameTitles.Game
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun GamesListScreen(
    gamesListViewModel: GamesListViewModel = viewModel(),
    goToGameDetails: (Game) -> Unit,
    goToGameEditProfile: (Game) -> Unit,
    startGame: (Game) -> Unit,
    goToSettings: () -> Unit,
    goToTitleManager: () -> Unit,
    goToAboutCemu: () -> Unit,
    goToMedia: () -> Unit,
    goToDownload: () -> Unit,
    tryCreateShortcut: (Game) -> Boolean,
) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val lifecycleState by lifecycleOwner.lifecycle.currentStateFlow.collectAsState()
    val coroutineScope = rememberCoroutineScope()
    var refreshing by remember { mutableStateOf(false) }
    val snackbarHostState = remember { SnackbarHostState() }
    val games by gamesListViewModel.games.collectAsStateWithLifecycle()
    val context = LocalContext.current
    var sidebarExpanded by rememberSaveable { mutableStateOf(false) }

    val state = rememberPullToRefreshState()

    LaunchedEffect(lifecycleState) {
        if (lifecycleState == Lifecycle.State.RESUMED && gamesListViewModel.gamePathsHaveChanged())
            gamesListViewModel.refreshGames()
    }

    DisposableEffect(lifecycleOwner) {
        onDispose {
            gamesListViewModel.setFilterText("")
        }
    }

    Scaffold(
        containerColor = SteamDeckColors.Background,
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        topBar = {
            TopAppBar(
                navigationIcon = {
                    SidebarToggleButton(
                        expanded = sidebarExpanded,
                        onClick = { sidebarExpanded = !sidebarExpanded },
                    )
                },
                title = { Text(tr("Library")) },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = SteamDeckColors.Background,
                    titleContentColor = SteamDeckColors.TextPrimary,
                ),
            )
        },
    ) { scaffoldPadding ->
        Row(
            modifier = Modifier
                .padding(scaffoldPadding)
                .fillMaxSize(),
        ) {
            AppSidebar(
                expanded = sidebarExpanded,
                activeDestination = SidebarDestination.LIBRARY,
                onCloseEmulator = { (context as? Activity)?.finish() },
                actions = rememberDefaultSidebarActions(
                    goToLibrary = {},
                    goToMedia = goToMedia,
                    goToDownload = goToDownload,
                    goToSettings = goToSettings,
                    goToTitleManager = goToTitleManager,
                    openCemuFolder = {
                        if (!tryOpenCemuFolder(context)) {
                            snackbarHostState.showMessage(
                                coroutineScope,
                                tr("Failed to open Cemu folder")
                            )
                        }
                    },
                    shareLogFile = {
                        if (!logFileExists()) {
                            snackbarHostState.showMessage(coroutineScope, tr("Log file doesn't exist"))
                        } else if (!tryShareLogFile(context)) {
                            snackbarHostState.showMessage(coroutineScope, tr("Failed to open log file"))
                        }
                    },
                    goToAboutCemu = goToAboutCemu,
                ),
            )

            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .pullToRefresh(
                        isRefreshing = refreshing,
                        state = state,
                        onRefresh = {
                            coroutineScope.launch {
                                refreshing = true
                                gamesListViewModel.refreshGames()
                                delay(1500)
                                refreshing = false
                            }
                        },
                    ),
            ) {
                GameList(
                    games = games,
                    setFavorite = gamesListViewModel::setGameTitleFavorite,
                    deleteShaderCaches = {
                        gamesListViewModel.removeShadersForGame(it)
                        snackbarHostState.showMessage(coroutineScope, tr("Shader caches removed"))
                    },
                    startGame = startGame,
                    goToGameDetails = goToGameDetails,
                    goToGameEditProfile = goToGameEditProfile,
                    createShortcut = {
                        if (!tryCreateShortcut(it)) {
                            snackbarHostState.showMessage(
                                coroutineScope,
                                tr("Couldn't create shortcut for game")
                            )
                        }
                    },
                )

                PullToRefreshDefaults.Indicator(
                    modifier = Modifier.align(Alignment.TopCenter),
                    isRefreshing = refreshing,
                    state = state,
                    containerColor = MaterialTheme.colorScheme.surfaceVariant,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun GameList(
    games: List<Game>,
    startGame: (Game) -> Unit,
    goToGameDetails: (Game) -> Unit,
    goToGameEditProfile: (Game) -> Unit,
    setFavorite: (Game, Boolean) -> Unit,
    createShortcut: (Game) -> Unit,
    deleteShaderCaches: (Game) -> Unit,
) {
    LazyVerticalGrid(
        modifier = Modifier
            .padding(8.dp)
            .fillMaxSize(),
        columns = GridCells.Adaptive(150.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        items(items = games, key = { it.path }) { game ->
            var showDeleteShaderConfirmationDialog by remember { mutableStateOf(false) }
            GameListItem(
                modifier = Modifier.animateItem(),
                game = game,
                onStartGame = startGame,
                onIsFavoriteChanged = { isFavorite ->
                    setFavorite(game, isFavorite)
                },
                onEditGameProfile = {
                    goToGameEditProfile(game)
                },
                onRemoveShaderCaches = { showDeleteShaderConfirmationDialog = true },
                onAboutTitle = {
                    goToGameDetails(game)
                },
                onCreateShortcut = {
                    createShortcut(game)
                },
            )

            if (showDeleteShaderConfirmationDialog) {
                ShaderCachesConfirmationDialog(
                    gameName = game.name ?: "",
                    onDismissRequest = { showDeleteShaderConfirmationDialog = false },
                    onConfirm = {
                        deleteShaderCaches(game)
                        showDeleteShaderConfirmationDialog = false
                    },
                )
            }
        }
    }
}

@Composable
private fun ShaderCachesConfirmationDialog(
    gameName: String,
    onDismissRequest: () -> Unit,
    onConfirm: () -> Unit,
) {
    AlertDialog(
        title = { Text(tr("Remove shader caches")) },
        text = { Text(tr("Remove the shader caches for {0}?", gameName)) },
        dismissButton = { TextButton(onClick = onDismissRequest) { Text(tr("No")) } },
        onDismissRequest = onDismissRequest,
        confirmButton = { TextButton(onClick = onConfirm) { Text(tr("Yes")) } },
    )
}

@Composable
private fun GameListItem(
    onStartGame: (Game) -> Unit,
    onIsFavoriteChanged: (Boolean) -> Unit,
    onEditGameProfile: () -> Unit,
    onRemoveShaderCaches: () -> Unit,
    onAboutTitle: () -> Unit,
    onCreateShortcut: () -> Unit,
    game: Game,
    modifier: Modifier = Modifier,
) {
    var contextMenuExpanded by rememberSaveable { mutableStateOf(false) }
    Box(
        modifier = modifier
            .aspectRatio(0.8f)
            .clip(RoundedCornerShape(6.dp))
            .background(Color(0xFF2A475E))
            .combinedClickable(
                onClick = { onStartGame(game) },
                onLongClick = { contextMenuExpanded = true },
            ),
    ) {
        // capa/ícone real do jogo preenchendo o card inteiro
        if (game.icon != null) {
            Image(
                modifier = Modifier.fillMaxSize(),
                bitmap = game.icon!!,
                contentScale = ContentScale.Crop,
                contentDescription = null,
            )
        } else {
            Icon(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(24.dp),
                painter = painterResource(R.drawable.ic_question_mark),
                contentDescription = null,
            )
        }

        // faixa escura com o nome do jogo, no rodapé do card
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .background(
                    Brush.verticalGradient(
                        colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.85f)),
                    )
                )
                .padding(horizontal = 6.dp, vertical = 4.dp),
        ) {
            Text(
                text = game.name ?: "",
                color = Color.White,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        if (game.isFavorite) {
            Icon(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(4.dp)
                    .size(20.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.surfaceVariant),
                painter = painterResource(R.drawable.ic_favorite),
                tint = MaterialTheme.colorScheme.primary,
                contentDescription = null
            )
        }

        GameContextMenu(
            expanded = contextMenuExpanded,
            onDismissRequest = { contextMenuExpanded = false },
            game = game,
            onIsFavoriteChanged = onIsFavoriteChanged,
            onEditGameProfile = onEditGameProfile,
            onRemoveShaderCaches = onRemoveShaderCaches,
            onAboutTitle = onAboutTitle,
            onCreateShortcut = onCreateShortcut,
        )
    }
}

@Composable
private fun GameContextMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    onIsFavoriteChanged: (Boolean) -> Unit,
    onEditGameProfile: () -> Unit,
    onRemoveShaderCaches: () -> Unit,
    onAboutTitle: () -> Unit,
    onCreateShortcut: () -> Unit,
    game: Game,
) {
    @Composable
    fun GameContextMenuItem(
        onClick: () -> Unit,
        text: String,
        enabled: Boolean = true,
        trailingIcon: @Composable (() -> Unit)? = null,
    ) {
        DropdownMenuItem(
            enabled = enabled,
            onClick = {
                onDismissRequest()
                onClick()
            },
            text = {
                Text(text = text)
            },
            trailingIcon = trailingIcon,
        )
    }
    DropdownMenu(expanded = expanded, onDismissRequest = onDismissRequest) {
        val gameTitleHasCaches = rememberSaveable {
            NativeGameTitles.titleHasShaderCacheFiles(game.titleId)
        }
        GameContextMenuItem(
            onClick = { onIsFavoriteChanged(!game.isFavorite) },
            text = tr("Favorite"),
            trailingIcon = { Checkbox(checked = game.isFavorite, onCheckedChange = null) },
        )
        GameContextMenuItem(
            onClick = onEditGameProfile,
            text = tr("Edit game profile"),
        )
        GameContextMenuItem(
            enabled = gameTitleHasCaches,
            onClick = onRemoveShaderCaches,
            text = tr("Remove shader caches")
        )
        GameContextMenuItem(
            onClick = onAboutTitle,
            text = tr("About title"),
        )
        GameContextMenuItem(
            onClick = onCreateShortcut,
            text = tr("Create shortcut"),
        )
    }
}

