package info.cemu.cemu.emulation

import android.annotation.SuppressLint
import android.os.Build
import android.os.PowerManager
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.getSystemService
import androidx.lifecycle.viewmodel.MutableCreationExtras
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.R
import info.cemu.cemu.common.settings.GamePadPosition
import info.cemu.cemu.common.settings.HotkeyAction
import info.cemu.cemu.common.ui.extensions.showMessage
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.emulation.emulatedusbdevices.EmulatedUSBDevicesDialog
import info.cemu.cemu.emulation.input.HotkeyManager
import info.cemu.cemu.emulation.inputoverlay.InputOverlaySurface
import info.cemu.cemu.emulation.inputoverlay.InputOverlaySurfaceView
import info.cemu.cemu.emulation.inputoverlay.InputOverlaySurfaceView.InputMode.DEFAULT
import info.cemu.cemu.emulation.inputoverlay.InputOverlaySurfaceView.InputMode.EDIT_POSITION
import info.cemu.cemu.emulation.inputoverlay.InputOverlaySurfaceView.InputMode.EDIT_SIZE
import info.cemu.cemu.nativeinterface.NativeCheats
import info.cemu.cemu.nativeinterface.NativeEmulation
import info.cemu.cemu.nativeinterface.NativeRenderer
import info.cemu.cemu.nativeinterface.NativeSettings
import kotlinx.coroutines.launch

@Composable
fun EmulationScreen(
    gamePath: String,
    setMotionSensorEnabled: (Boolean) -> Unit,
    setInputListeningEnabled: (Boolean) -> Unit,
    onQuit: () -> Unit,
    viewModel: EmulationViewModel = viewModel(
        factory = EmulationViewModel.Factory, extras = MutableCreationExtras().apply {
            set(EmulationViewModel.LAUNCH_PATH_KEY, gamePath)
        }),
) {
    val snackbarHostState = remember { SnackbarHostState() }
    val drawerState = rememberDrawerState(initialValue = DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    var showQuitConfirmationDialog by remember { mutableStateOf(false) }
    var inputOverlayInputMode by rememberSaveable { mutableStateOf(DEFAULT) }
    var showEmulatedUSBDevices by remember { mutableStateOf(false) }
    // Painel lateral DIREITO aberto em jogo (um por vez): Frame Gen, HUD/FPS, Cheats ou Controle. Ver InGamePanels.kt.
    var sidePanel by rememberSaveable { mutableIntStateOf(InGamePanel.NONE) }
    var frameGenOn by remember { mutableStateOf(NativeSettings.getFsr3FrameGen()) }

    val emulationError by viewModel.emulationError.collectAsState()
    val isEmulationInitialized by viewModel.isEmulationInitialized.collectAsState()
    val sideMenuState by viewModel.sideMenuState.collectAsState()
    val gamePadPosition by viewModel.gamePadPosition.collectAsState()
    val isInputOverlayVisible by viewModel.isInputOverlayVisible.collectAsState()
    val inputOverlaySettings by viewModel.inputOverlaySettings.collectAsState()


    fun closeDrawer() {
        scope.launch {
            drawerState.close()
        }
    }

    suspend fun toggleMenu() {
        drawerState.apply {
            if (isClosed) {
                open()
            } else {
                close()
            }
        }
    }

    BackHandler {
        if (drawerState.isAnimationRunning) {
            return@BackHandler
        }

        scope.launch {
            toggleMenu()
        }
    }

    LaunchedEffect(drawerState.isClosed) {
        setInputListeningEnabled(drawerState.isClosed)
    }

    // Quality of Life: pushes the device's thermal status to the native overlay for the "Device
    // thermal status" HUD stat. Only a coarse level (Nominal..Shutdown) is available to a normal
    // app - there's no public API for a real temperature reading in degrees. The public API for
    // this only exists from Android 10 (API 29) onward; below that, this is simply skipped -
    // the HUD toggle can safely stay on, it just never shows anything on those devices.
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        val context = LocalContext.current
        DisposableEffect(context) {
            val powerManager = context.getSystemService<PowerManager>()
            val listener = PowerManager.OnThermalStatusChangedListener { status ->
                NativeEmulation.updateThermalStatus(status)
            }
            powerManager?.addThermalStatusListener(listener)
            powerManager?.let { listener.onThermalStatusChanged(it.currentThermalStatus) }
            onDispose {
                powerManager?.removeThermalStatusListener(listener)
            }
        }
    }

    LaunchedEffect(Unit) {
        HotkeyManager.actions.collect { action ->
            when (action) {
                HotkeyAction.QUIT -> showQuitConfirmationDialog = true
                HotkeyAction.TOGGLE_MENU -> toggleMenu()
                HotkeyAction.SHOW_EMULATED_USB_DEVICES_DIALOG -> showEmulatedUSBDevices = true
            }
        }
    }

    val onSideMenuStateChange: (SideMenuState) -> Unit = {
        viewModel.updateSideMenuState(it)
        setMotionSensorEnabled(it.isMotionEnabled)
        NativeEmulation.setReplaceTVWithPadView(it.isTVReplacedWithPad)
    }

    ModalNavigationDrawer(
        drawerState = drawerState,
        gesturesEnabled = drawerState.isOpen,
        drawerContent = {
            ModalDrawerSheet {
                Column(
                    modifier = Modifier
                        .padding(horizontal = 8.dp)
                        .width(IntrinsicSize.Max)
                        .verticalScroll(rememberScrollState())
                ) {
                    EmulationSideMenuContent(
                        sideMenuState = sideMenuState,
                        updateState = {
                            onSideMenuStateChange(it)
                            closeDrawer()
                        },
                        onEditInputOverlay = {
                            snackbarHostState.showMessage(scope, tr("Edit input positions"))
                            inputOverlayInputMode = EDIT_POSITION
                            closeDrawer()
                        },
                        onResetInputOverlay = {
                            viewModel.resetInputOverlayLayout()
                            closeDrawer()
                        },
                        onTakeScreenshot = {
                            snackbarHostState.showMessage(scope, tr("Taking screenshot..."))
                            NativeRenderer.triggerScreenshot()
                            closeDrawer()
                        },
                        onQuit = {
                            showQuitConfirmationDialog = true
                            closeDrawer()
                        },
                        onShowEmulatedUSBDevices = {
                            showEmulatedUSBDevices = true
                            closeDrawer()
                        },
                        onShowControllerPanel = {
                            sidePanel = InGamePanel.CONTROLLER
                            closeDrawer()
                        },
                        onShowInGameCheats = {
                            sidePanel = InGamePanel.CHEATS
                            closeDrawer()
                        },
                        onShowHudPanel = {
                            sidePanel = InGamePanel.HUD
                            closeDrawer()
                        },
                        frameGenPanelUnlocked = sidePanel == InGamePanel.FRAME_GEN,
                        onFrameGenPanelUnlockedChange = {
                            sidePanel = if (it) InGamePanel.FRAME_GEN else InGamePanel.NONE
                            closeDrawer()
                        },
                    )
                }
            }
        },
    ) {
        EmulationSurfaces(
            sideMenuState = sideMenuState,
            gamePadPosition = gamePadPosition,
            mainHolderCallback = viewModel.mainHolderCallback,
            padHolderCallback = viewModel.padHolderCallback,
            onInitializeEmulation = viewModel::initializeEmulation,
        )

        InputOverlaySurface(
            isVisible = isInputOverlayVisible,
            inputOverlaySettings = inputOverlaySettings,
            inputMode = inputOverlayInputMode,
            onEditFinished = { viewModel.saveInputOverlayRectangles(it) },
        )

        if (inputOverlayInputMode != DEFAULT) {
            EditInputsLayout(
                inputMode = inputOverlayInputMode,
                onFinishClick = {
                    snackbarHostState.showMessage(scope, tr("Exited input edit mode"))
                    inputOverlayInputMode = DEFAULT
                },
                onMoveClick = {
                    snackbarHostState.showMessage(scope, tr("Edit input positions"))
                    inputOverlayInputMode = EDIT_POSITION
                },
                onResizeClick = {
                    snackbarHostState.showMessage(scope, tr("Edit input size"))
                    inputOverlayInputMode = EDIT_SIZE
                },
            )
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        when (sidePanel) {
            InGamePanel.FRAME_GEN -> FrameGenInGamePanel(
                modifier = Modifier.align(Alignment.CenterEnd),
                frameGenEnabled = frameGenOn,
                onFrameGenChange = {
                    frameGenOn = it
                    NativeSettings.setFsr3FrameGen(it)
                },
                onClose = { sidePanel = InGamePanel.NONE },
            )

            InGamePanel.HUD -> HudInGamePanel(
                modifier = Modifier.align(Alignment.CenterEnd),
                onClose = { sidePanel = InGamePanel.NONE },
            )

            InGamePanel.CHEATS -> CheatsInGamePanel(
                modifier = Modifier.align(Alignment.CenterEnd),
                onClose = { sidePanel = InGamePanel.NONE },
            )

            InGamePanel.CONTROLLER -> ControllerInGamePanel(
                modifier = Modifier.align(Alignment.CenterEnd),
                sideMenuState = sideMenuState,
                onSideMenuStateChange = onSideMenuStateChange,
                onEditInputOverlay = {
                    snackbarHostState.showMessage(scope, tr("Edit input positions"))
                    inputOverlayInputMode = EDIT_POSITION
                    sidePanel = InGamePanel.NONE // fecha o painel para poder arrastar os botoes
                },
                onResetInputOverlay = { viewModel.resetInputOverlayLayout() },
                onClose = { sidePanel = InGamePanel.NONE },
            )
        }
        SnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(16.dp)
        )
    }

    emulationError?.let {
        EmulationErrorDialog(it, onQuit)
    }

    if (!isEmulationInitialized) {
        EmulationLoadingDialog()
    }

    if (showQuitConfirmationDialog) {
        EmulationQuitConfirmationDialog(
            onQuit = onQuit,
            onDismiss = { showQuitConfirmationDialog = false },
        )
    }

    if (showEmulatedUSBDevices) {
        EmulatedUSBDevicesDialog(
            onDismiss = { showEmulatedUSBDevices = false },
        )
    }

    EmulationTextInputDialog()
}

@Composable
private fun EditInputsLayout(
    inputMode: InputOverlaySurfaceView.InputMode,
    onFinishClick: () -> Unit,
    onMoveClick: () -> Unit,
    onResizeClick: () -> Unit
) {
    Box(
        modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Button(onClick = onFinishClick) { Text(tr("Done")) }

            Row(
                modifier = Modifier
                    .wrapContentSize()
                    .padding(8.dp),
                horizontalArrangement = Arrangement.spacedBy(36.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                FilledIconButton(enabled = inputMode != EDIT_POSITION, onClick = onMoveClick) {
                    Icon(
                        painter = painterResource(id = R.drawable.ic_move),
                        contentDescription = tr("Move")
                    )
                }

                FilledIconButton(enabled = inputMode != EDIT_SIZE, onClick = onResizeClick) {
                    Icon(
                        painter = painterResource(id = R.drawable.ic_resize),
                        contentDescription = tr("Resize"),
                    )
                }
            }
        }
    }
}

@Composable
private fun EmulationSideMenuContent(
    sideMenuState: SideMenuState,
    updateState: (SideMenuState) -> Unit,
    onShowEmulatedUSBDevices: () -> Unit,
    onShowControllerPanel: () -> Unit,
    onShowInGameCheats: () -> Unit,
    onShowHudPanel: () -> Unit,
    frameGenPanelUnlocked: Boolean,
    onFrameGenPanelUnlockedChange: (Boolean) -> Unit,
    onEditInputOverlay: () -> Unit,
    onResetInputOverlay: () -> Unit,
    onTakeScreenshot: () -> Unit,
    onQuit: () -> Unit,
) {
    CheckboxItem(
        label = tr("Enable motion"),
        checked = sideMenuState.isMotionEnabled,
        onCheckedChange = { updateState(sideMenuState.copy(isMotionEnabled = it)) },
    )

    CheckboxItem(
        label = tr("Replace TV with PAD"),
        checked = sideMenuState.isTVReplacedWithPad,
        onCheckedChange = { updateState(sideMenuState.copy(isTVReplacedWithPad = it)) },
    )

    CheckboxItem(
        label = tr("Show PAD"),
        checked = sideMenuState.isPadVisible,
        onCheckedChange = { updateState(sideMenuState.copy(isPadVisible = it)) },
    )

    TextButtonItem(
        label = tr("Emulated USB Devices"),
        onClick = onShowEmulatedUSBDevices,
    )

    // Abre o painel lateral direito com tudo que da para mudar nos controles durante o jogo (inclui a troca de perfil).
    TextButtonItem(
        label = tr("Controller"),
        onClick = onShowControllerPanel,
    )

    if (NativeCheats.isInGameCheatsEnabled()) {
        TextButtonItem(
            label = tr("Cheats"),
            onClick = onShowInGameCheats,
        )
    }

    // O liga/desliga do Frame Gen fica dentro do painel em jogo (nao precisa de item separado aqui).
    CheckboxItem(
        label = tr("FSR3 Frame Gen (painel em jogo)"),
        checked = frameGenPanelUnlocked,
        onCheckedChange = onFrameGenPanelUnlockedChange,
    )

    // Abre o painel lateral direito com todas as opcoes do HUD (FPS, frame time, CPU, RAM, VRAM...), aplicadas na hora.
    TextButtonItem(
        label = tr("Show FPS / HUD"),
        onClick = onShowHudPanel,
    )

    CheckboxItem(
        label = tr("Show input overlay"),
        checked = sideMenuState.isInputOverlayVisible,
        onCheckedChange = { updateState(sideMenuState.copy(isInputOverlayVisible = it)) },
    )

    TextButtonItem(
        label = tr("Edit inputs"),
        enabled = sideMenuState.isInputOverlayVisible,
        onClick = onEditInputOverlay,
    )

    TextButtonItem(
        label = tr("Reset input overlay"),
        enabled = sideMenuState.isInputOverlayVisible,
        onClick = onResetInputOverlay,
    )

    // Quality of Life (Experimental/Hackers), off by default: only shown once explicitly enabled
    // in the Quality of Life settings screen.
    if (NativeRenderer.isQuickScreenshotEnabled()) {
        TextButtonItem(
            label = tr("Take Screenshot"),
            onClick = onTakeScreenshot,
        )
    }

    TextButtonItem(
        label = tr("Exit"),
        onClick = onQuit,
    )
}

@Composable
private fun CheckboxItem(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    enabled: Boolean = true,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .alpha(if (enabled) 1f else 0.6f)
            .clickable(enabled) { onCheckedChange(!checked) }
            .padding(8.dp)
            .minimumInteractiveComponentSize(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            modifier = Modifier
                .padding(end = 8.dp)
                .weight(1f),
            fontSize = 16.sp,
        )

        Checkbox(
            checked = checked,
            onCheckedChange = null,
        )
    }
}

@Composable
private fun TextButtonItem(
    label: String,
    onClick: () -> Unit,
    enabled: Boolean = true,
) {
    Text(
        text = label,
        modifier = Modifier
            .fillMaxWidth()
            .alpha(if (enabled) 1f else 0.6f)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(8.dp)
            .heightIn(min = 48.dp)
            .wrapContentHeight(align = Alignment.CenterVertically),
        fontSize = 16.sp,
    )
}

@Composable
private fun EmulationSurfaces(
    sideMenuState: SideMenuState,
    gamePadPosition: GamePadPosition?,
    mainHolderCallback: SurfaceHolder.Callback,
    padHolderCallback: SurfaceHolder.Callback,
    onInitializeEmulation: () -> Unit
) {
    if (gamePadPosition == null) {
        return
    }

    val isVertical = gamePadPosition.isVertical()
    val appearsAfterTV = gamePadPosition.appearsAfterTV()
    val isPadVisible = sideMenuState.isPadVisible

    @Composable
    fun MainSurface(modifier: Modifier) {
        EmulationSurface(
            modifier = modifier,
            isTV = true,
            holderCallback = mainHolderCallback,
            afterInit = { onInitializeEmulation() },
        )
    }

    @Composable
    fun PadSurface(modifier: Modifier) {
        if (isPadVisible) {
            EmulationSurface(
                modifier = modifier,
                isTV = false,
                holderCallback = padHolderCallback,
            )
        }
    }

    @Composable
    fun SurfacesInOrder(itemModifier: Modifier) {
        if (appearsAfterTV) {
            MainSurface(itemModifier)
            PadSurface(itemModifier)
        } else {
            PadSurface(itemModifier)
            MainSurface(itemModifier)
        }
    }

    LinearLayout(isVertical) { itemModifier ->
        SurfacesInOrder(itemModifier)
    }
}

@Composable
private fun LinearLayout(
    isVertical: Boolean,
    content: @Composable (Modifier) -> Unit,
) {
    if (isVertical) {
        Column(modifier = Modifier.fillMaxSize()) {
            content(Modifier.weight(1f))
        }
    } else {
        CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
            Row(modifier = Modifier.fillMaxSize()) {
                content(Modifier.weight(1f))
            }
        }
    }
}

@Composable
@SuppressLint("ClickableViewAccessibility")
private fun EmulationSurface(
    modifier: Modifier,
    isTV: Boolean,
    holderCallback: SurfaceHolder.Callback,
    afterInit: () -> Unit = {}
) {
    AndroidView(
        modifier = modifier,
        factory = { context ->
            SurfaceView(context).apply {
                var firstChange = true

                setOnTouchListener(CanvasOnTouchListener(isTV))

                holder.addCallback(holderCallback)

                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceChanged(
                        holder: SurfaceHolder, format: Int, width: Int, height: Int
                    ) {
                        if (firstChange) {
                            afterInit()
                            firstChange = false
                        }
                    }

                    override fun surfaceCreated(holder: SurfaceHolder) {}

                    override fun surfaceDestroyed(holder: SurfaceHolder) {}
                })
            }
        })
}

@Composable
private fun EmulationQuitConfirmationDialog(onQuit: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        title = { Text(tr("Exit confirmation")) },
        text = { Text(tr("Are you sure you want to exit?")) },
        confirmButton = { TextButton(onClick = onQuit) { Text(tr("Yes")) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(tr("No")) } },
        onDismissRequest = onDismiss,
    )
}

@Composable
private fun EmulationLoadingDialog() {
    AlertDialog(
        title = { Text(tr("Initializing emulation")) },
        text = { LinearProgressIndicator(modifier = Modifier.fillMaxWidth()) },
        confirmButton = {},
        onDismissRequest = {},
    )
}

@Composable
private fun EmulationErrorDialog(error: NativeError, onQuit: () -> Unit) {
    val errorMessage = remember(error) {
        when (error) {
            is NativeError.SystemInitializationError -> tr("Failed to initialize")

            is NativeError.RendererInitializationError ->
                tr("Failed creating renderer: {0}", error.message)

            is NativeError.SurfaceCreationError -> tr("Failed creating surface: {0}", error.message)

            NativeError.GameFilesNotFoundError -> tr("Unable to launch game because the base files were not found.")
            NativeError.NoDiscKeysError -> tr("Could not decrypt title. Make sure that keys.txt contains the correct disc key for this title.")
            NativeError.NoTitleTikError -> tr("Could not decrypt title because title.tik is missing.")
            is NativeError.UnknownTilePrepareError ->
                tr("Unable to launch game\nPath: {0}", error.launchPath) +
                    (if (error.launchPath.startsWith("content://") && !error.launchPath.contains("com.android.externalstorage"))
                        "\n\n" + tr("Este jogo foi aberto por outro app (explorador/arquivador), que muitas vezes nao permite ler o arquivo direto. Feche e adicione a pasta dos jogos na biblioteca (tela inicial > Escolher pasta de jogos) e abra o jogo por la.")
                    else "")

            NativeError.LaunchingTitleError -> tr("Failed to launch title")
        }
    }
    AlertDialog(
        title = { Text(tr("Error")) },
        text = { Text(errorMessage) },
        confirmButton = { TextButton(onClick = onQuit) { Text(tr("Quit")) } },
        onDismissRequest = {},
    )
}
