import QtQuick
import QtQuick.Window
import QtMultimedia
import ArcadeNative 1.0

// ============================================================================
//  Menú estilo multijuegos arcade (Pandora / NeoGeo, principios de los 2000).
//  Este archivo es todo el diseño: cambia colores, tamaños y textos aquí.
// ============================================================================
Window {
    id: win
    width: 1280; height: 720
    visible: false // main.cpp elige el monitor y la muestra (pantalla completa o ventana)
    title: App.cabinetName
    color: "black"

    // ---------------- Tema ----------------
    // Los colores vienen del tema activo (OPCIONES > TEMA; ver src/theme.cpp y themes/ejemplo/theme.ini)
    readonly property color cBg1: Theme.c.bg1
    readonly property color cBg2: Theme.c.bg2
    readonly property color cAccent: Theme.c.accent      // barra de selección
    readonly property color cAccent2: Theme.c.accent2    // números y títulos de panel
    readonly property color cOnAccent: Theme.c.onAccent  // texto sobre la barra de selección
    readonly property color cText: Theme.c.text
    readonly property color cDim: Theme.c.dim
    readonly property color cPanel: Theme.c.panel
    readonly property color cBorder: Theme.c.border
    readonly property real  u: Math.min(width / 1280, height / 720) // unidad de escala

    property int current: Math.min(App.lastIndex, Math.max(0, Games.count - 1))
    property int listRev: 0 // cambia cuando cambia el filtro, para refrescar el juego seleccionado
    property var game: (listRev, Games.count > 0 ? Games.get(current) : ({}))
    property bool optionsOpen: false
    property bool searchOpen: false
    property bool remapOpen: false
    // Pantalla de inicio: se elige el sistema (o todos / favoritos / recientes) y luego se entra a su lista
    property bool homeOpen: true
    property var cards: []
    function refreshCards() { cards = Games.systemCards() }
    function goHome() {
        refreshCards()
        var i = 0
        for (var k = 0; k < cards.length; ++k) if (cards[k].id === Games.system) i = k
        home.index = i
        searchOpen = false
        homeOpen = true
    }
    function enterSystem(id) {
        var src = Games.sourceRow(current)
        Games.search = ""
        Games.system = id
        App.lastSystem = id
        homeOpen = false
        var row = Games.rowOfSource(src >= 0 ? src : App.lastIndex)
        select(row >= 0 ? row : 0)
    }
    property string errorText: ""
    readonly property var aspectNames: ["ORIGINAL", "PÍXELES EXACTOS", "ESTIRADA"]
    readonly property var crtNames: ["NO", "PLANO", "CURVO"]

    // ---------------- Modo atracción ----------------
    // Si nadie toca nada durante un rato, el menú va saltando solo de juego en juego como una
    // máquina de salón. Cualquier botón, tecla o movimiento del mouse lo detiene.
    property bool attractOn: false
    readonly property bool menuIdle: !App.gameRunning && !optionsOpen && !searchOpen && !remapOpen
                                     && errorText === "" && (homeOpen || Games.count > 1)
    Timer {
        id: idleTimer
        interval: App.attractSeconds * 1000
        running: App.attract && win.menuIdle && !win.attractOn
        onTriggered: win.attractOn = true
    }
    Timer {
        interval: 7000; repeat: true; triggeredOnStart: true
        running: win.attractOn && win.menuIdle
        onTriggered: {
            if (win.homeOpen) home.index = Math.floor(Math.random() * win.cards.length)
            else win.select(Math.floor(Math.random() * Games.count))
        }
    }
    onMenuIdleChanged: if (!menuIdle) attractOn = false
    // Devuelve true si la entrada solo sirvió para despertar el menú
    function wake() {
        if (idleTimer.running) idleTimer.restart()
        if (!attractOn) return false
        attractOn = false
        return true
    }

    // ---------------- Entrada unificada (teclado + mandos) ----------------
    function act(a) {
        if (App.confirmingExit) { sfx(a); exitDlg.handle(a); return }
        if (App.paused) {
            if (Pad.capturing) return
            sfx(a)
            if (remapOpen) remap.handle(a); else pauseMenu.handle(a) // los controles también se abren desde la pausa
            return
        }
        if (App.gameRunning) return
        if (wake()) return
        if (!Pad.capturing) sfx(a)
        if (errorText !== "") { if (a === "accept" || a === "back") errorText = ""; return }
        if (Pad.capturing) return
        if (searchOpen) { search.handle(a); return }
        if (remapOpen) { remap.handle(a); return }
        if (optionsOpen) { options.handle(a); return }
        if (a === "options") { optionsOpen = true; return }
        if (homeOpen) { home.handle(a); return }
        switch (a) {
        case "up":       move(-1); break
        case "down":     move(1); break
        case "left":     move(-10); break
        case "right":    move(10); break
        case "pageUp":   select(Games.jumpLetter(current, -1)); break
        case "pageDown": select(Games.jumpLetter(current, 1)); break
        case "accept":   if (Games.count > 0) { clickSfx(); App.launch(current) } break
        case "back":     goHome(); break // las opciones se abren desde la pantalla de sistemas (o con F1)
        case "search":     search.index = 0; searchOpen = true; break
        case "systemPrev": changeSystem(-1); break
        case "systemNext": changeSystem(1); break
        case "favorite":   toggleFavorite(); break
        }
    }
    function toggleFavorite() {
        if (Games.count === 0) return
        var title = game.title, src = Games.sourceRow(current)
        var on = Games.toggleFavorite(current)
        toast.show(on ? "★ " + title + " AÑADIDO A FAVORITOS" : title + " QUITADO DE FAVORITOS")
        var row = Games.rowOfSource(src) // en la lista de favoritos el juego desaparece al quitarlo
        select(row >= 0 ? row : Math.max(0, Math.min(current, Games.count - 1)))
    }
    function playTimeText(secs) {
        if (secs < 60) return "MENOS DE 1 MIN"
        var h = Math.floor(secs / 3600), m = Math.floor((secs % 3600) / 60)
        return h > 0 ? h + " H " + m + " MIN" : m + " MIN"
    }
    // Cambia el filtro conservando el juego seleccionado si sigue visible
    function refilter(change) {
        var src = Games.sourceRow(current)
        change()
        var row = Games.rowOfSource(src)
        select(row >= 0 ? row : 0)
    }
    function changeSystem(dir) {
        refilter(function () { Games.cycleSystem(dir) })
    }
    function setSearch(text) { Games.search = text; select(0) }
    function move(d) {
        if (Games.count === 0) return
        var n = current + d
        if (Math.abs(d) === 1) n = (n + Games.count) % Games.count   // la lista da la vuelta
        else n = Math.max(0, Math.min(Games.count - 1, n))
        select(n)
    }
    function select(i) { current = i; list.positionViewAtIndex(i, ListView.Contain) }
    function clickSfx() {}

    // ---------------- Sonidos y música del menú ----------------
    // Los .wav están en la carpeta sounds/ (se crean unos sencillos si faltan; puedes cambiarlos).
    // La música es opcional: sounds/musica.mp3 (u .ogg/.wav) suena en el menú y calla al jugar.
    SoundEffect { id: sfxMove; source: App.soundUrl("mover"); volume: App.volume / 100 * 0.4 }
    SoundEffect { id: sfxAccept; source: App.soundUrl("aceptar"); volume: App.volume / 100 * 0.5 }
    SoundEffect { id: sfxBack; source: App.soundUrl("volver"); volume: App.volume / 100 * 0.5 }
    function sfx(a) {
        if (!App.menuSounds) return
        if (a === "accept") sfxAccept.play()
        else if (a === "back" || a === "pause") sfxBack.play()
        else if (a !== "") sfxMove.play()
    }
    MediaPlayer {
        id: music
        source: App.musicUrl
        loops: MediaPlayer.Infinite
        audioOutput: AudioOutput { volume: App.volume / 100 * 0.35 }
        readonly property bool wanted: App.menuMusic && App.musicUrl !== "" && !App.gameRunning
        onWantedChanged: wanted ? play() : pause()
        Component.onCompleted: if (wanted) play()
    }

    Connections {
        target: Games
        function onFilterChanged() { win.listRev++ }
        function onSystemsChanged() { win.refreshCards() }
        function onDataChanged() { win.listRev++ } // favorito / veces jugado del juego seleccionado
    }
    Connections {
        target: App
        function onMenuAction(a) { win.act(a) }
        function onError(t) { win.errorText = t }
        function onToast(t) { toast.show(t) }
        function onPausedChanged() { if (!App.paused) win.remapOpen = false }
        function onGameRunningChanged() {
            win.remapOpen = false
            if (App.gameRunning) emu.forceActiveFocus()
            else { menuRoot.forceActiveFocus(); preview.restart() }
        }
    }

    // Cursor: en pantalla completa aparece al mover el mouse y se oculta tras 3 s quieto;
    // dentro del juego solo se muestra mientras está el aviso de salir.
    property bool mouseActive: false
    Timer { id: mouseIdle; interval: 3000; onTriggered: win.mouseActive = false }
    MouseArea {
        anchors.fill: parent; z: 100
        hoverEnabled: true; acceptedButtons: Qt.NoButton // solo observa: los clics pasan a lo de abajo
        cursorShape: !App.fullscreen || (win.mouseActive && (!App.gameRunning || App.confirmingExit || App.paused))
                     ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: { win.mouseActive = true; mouseIdle.restart(); win.wake() }
    }

    // =======================================================================
    //  MENÚ
    // =======================================================================
    Item {
        id: menuRoot
        anchors.fill: parent
        visible: !App.gameRunning
        focus: true

        Keys.onPressed: (e) => {
            const map = {}
            map[Qt.Key_Up] = "up"; map[Qt.Key_Down] = "down"
            map[Qt.Key_Left] = "left"; map[Qt.Key_Right] = "right"
            map[Qt.Key_PageUp] = "pageUp"; map[Qt.Key_PageDown] = "pageDown"
            map[Qt.Key_Return] = "accept"; map[Qt.Key_Enter] = "accept"
            map[Qt.Key_Escape] = "back"; map[Qt.Key_Backspace] = "back"
            if (win.wake()) { e.accepted = true; return }
            if (e.key === Qt.Key_F1 && !Pad.capturing && !win.searchOpen) { win.act("options"); e.accepted = true; return }
            // Escribir en la pantalla de sistemas busca entre todos los juegos
            if (win.homeOpen && win.errorText === "" && !win.optionsOpen && !win.remapOpen && !Pad.capturing
                    && e.text.length === 1 && /[0-9a-zA-Z]/.test(e.text))
                win.enterSystem("")
            // Las letras y números no son atajos: en el menú escriben directo en la barra de búsqueda
            if (e.key === Qt.Key_F11) { App.fullscreen = !App.fullscreen; e.accepted = true; return }
            if ((e.key === Qt.Key_F2 || e.key === Qt.Key_Insert) && !Pad.capturing) { win.act("favorite"); e.accepted = true; return }
            if (Pad.capturing) { if (e.key === Qt.Key_Escape) Pad.cancelCapture(); e.accepted = true; return }
            if (win.searchOpen) {
                // Con teclado real se escribe directo; las flechas mueven el teclado en pantalla
                if (e.key === Qt.Key_Escape || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) win.searchOpen = false
                else if (e.key === Qt.Key_Backspace) search.backspace()
                else if (e.key === Qt.Key_Up) win.act("up")
                else if (e.key === Qt.Key_Down) win.act("down")
                else if (e.key === Qt.Key_Left) win.act("left")
                else if (e.key === Qt.Key_Right) win.act("right")
                else if (e.key === Qt.Key_PageUp) win.act("pageUp")
                else if (e.key === Qt.Key_PageDown) win.act("pageDown")
                else if (e.text.length === 1 && /[0-9a-zA-Z '\-]/.test(e.text)) search.type(e.text.toUpperCase())
                e.accepted = true
                return
            }
            if (win.errorText === "" && !win.optionsOpen && !win.remapOpen) {
                if (e.key === Qt.Key_Tab) { win.act("systemNext"); e.accepted = true; return }
                if (e.key === Qt.Key_Backtab) { win.act("systemPrev"); e.accepted = true; return }
                // Buscar sin abrir nada: escribir filtra, Retroceso borra y Esc limpia la búsqueda
                if (Games.search !== "" && e.key === Qt.Key_Backspace) { search.backspace(); e.accepted = true; return }
                if (Games.search !== "" && e.key === Qt.Key_Escape) { win.setSearch(""); e.accepted = true; return }
                if (e.key === Qt.Key_Backspace) { e.accepted = true; return }
                if (e.text.length === 1 && /[0-9a-zA-Z'\-]/.test(e.text)) { search.type(e.text.toUpperCase()); e.accepted = true; return }
                if (e.key === Qt.Key_Space && Games.search !== "") { search.type(" "); e.accepted = true; return }
            }
            if (map[e.key] !== undefined) { win.act(map[e.key]); e.accepted = true }
        }

        // Fondo con degradado y rejilla tipo pantalla de arcade
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: win.cBg1 }
                GradientStop { position: 1; color: win.cBg2 }
            }
        }
        // Imagen de fondo (del tema o de la carpeta fondos/), oscurecida para que el texto se lea
        Image {
            id: wallpaper
            anchors.fill: parent
            source: Theme.background
            visible: status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            asynchronous: true; smooth: true
            Rectangle { anchors.fill: parent; color: "black"; opacity: Theme.c.darken }
        }
        Grid {
            visible: Theme.c.showGrid
            anchors.fill: parent; opacity: 0.06
            columns: Math.ceil(parent.width / (40 * u)) + 1
            Repeater {
                model: Math.max(0, (Math.ceil(win.width / (40 * u)) + 1) * (Math.ceil(win.height / (40 * u)) + 1))
                Rectangle { width: 40 * u; height: 40 * u; color: "transparent"; border.color: Theme.c.grid; border.width: 1 }
            }
        }

        // ---------------- Encabezado ----------------
        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 64 * u
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.c.header1 }
                GradientStop { position: 0.5; color: Theme.c.header2 }
                GradientStop { position: 1; color: Theme.c.header1 }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 28 * u
                text: App.cabinetName
                font.family: arcadeFont; font.pixelSize: 30 * u; font.bold: true
                color: "white"; style: Text.Outline; styleColor: "#600010"
            }
            Text {
                anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 28 * u }
                text: win.homeOpen ? Games.total + " JUEGOS"
                                   : Games.count + (Games.count < Games.total ? " DE " + Games.total : "") + " JUEGOS"
                font.family: arcadeFont; font.pixelSize: 24 * u; font.bold: true
                color: win.cAccent; style: Text.Outline; styleColor: "#600010"
            }
            // Filtro activo: sistema (LT/RT o Tab) y búsqueda (X/□ o tecla F)
            Row {
                anchors.centerIn: parent
                spacing: 14 * u
                Rectangle {
                    width: sysText.width + 28 * u; height: 36 * u; radius: 18 * u
                    color: "#60000000"; border.color: Games.system !== "" ? win.cAccent : "#80ffffff"; border.width: 2 * u
                    Text {
                        id: sysText
                        anchors.centerIn: parent
                        text: win.homeOpen ? "ELIGE UN SISTEMA" : "◄ " + (Games.system !== "" ? Games.system : "TODOS LOS JUEGOS") + " ►"
                        font.family: arcadeFont; font.pixelSize: 18 * u; font.bold: true
                        color: Games.system !== "" ? win.cAccent : "white"
                    }
                    // Clic en la mitad izquierda = sistema anterior, derecha = siguiente
                    MouseArea { anchors.fill: parent; onClicked: (m) => win.act(m.x < width / 2 ? "systemPrev" : "systemNext") }
                }
                Rectangle { // volver a la pantalla de sistemas con el mouse
                    visible: !win.homeOpen
                    width: homeText.width + 24 * u; height: 36 * u; radius: 18 * u
                    color: "#60000000"; border.color: "#80ffffff"; border.width: 2 * u
                    Text {
                        id: homeText
                        anchors.centerIn: parent
                        text: "SISTEMAS"; color: "white"
                        font.family: arcadeFont; font.pixelSize: 15 * u; font.bold: true
                    }
                    MouseArea { anchors.fill: parent; onClicked: win.goHome() }
                }
            }
        }

        // ---------------- Lista de juegos ----------------
        Rectangle {
            id: listPanel
            anchors { top: header.bottom; left: parent.left; bottom: footer.top; margins: 20 * u }
            width: parent.width * 0.42
            visible: !win.homeOpen
            color: win.cPanel; border.color: win.cBorder; border.width: 2 * u; radius: 6 * u

            // Barra de búsqueda siempre visible: se escribe directo con el teclado,
            // o clic / Ⓧ para abrir el teclado en pantalla
            Rectangle {
                id: searchBar
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 8 * u }
                height: 44 * u; radius: 4 * u
                readonly property bool active: Games.search !== "" || win.searchOpen
                color: "black"; border.color: active ? win.cAccent : win.cBorder; border.width: 2 * u
                // Lupa dibujada (no depende de que la fuente tenga el símbolo)
                Item {
                    id: lens
                    anchors.verticalCenter: parent.verticalCenter; x: 12 * u
                    width: 24 * u; height: 24 * u
                    Rectangle { x: 1 * u; y: 1 * u; width: 15 * u; height: 15 * u; radius: 8 * u; color: "transparent"
                                border.color: searchBar.active ? win.cAccent : win.cDim; border.width: 2.5 * u }
                    Rectangle { x: 13 * u; y: 16 * u; width: 10 * u; height: 3 * u; rotation: 45; radius: 1 * u
                                color: searchBar.active ? win.cAccent : win.cDim }
                }
                Text {
                    anchors { verticalCenter: parent.verticalCenter; left: lens.right; leftMargin: 8 * u; right: clearBtn.left; rightMargin: 6 * u }
                    elide: Text.ElideRight
                    text: Games.search !== "" || win.searchOpen ? Games.search + (barBlink.on ? "_" : " ")
                                                               : "ESCRIBE PARA BUSCAR UN JUEGO…"
                    color: Games.search !== "" || win.searchOpen ? "white" : win.cDim
                    font.family: arcadeFont; font.pixelSize: 18 * u; font.bold: Games.search !== ""
                }
                Timer { id: barBlink; property bool on: true; interval: 450; repeat: true; running: searchBar.active; onTriggered: on = !on }
                MouseArea { anchors.fill: parent; onClicked: win.act("search") }
                // Botón para limpiar la búsqueda
                Rectangle {
                    id: clearBtn
                    anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 8 * u }
                    width: 30 * u; height: 30 * u; radius: 15 * u
                    visible: Games.search !== ""
                    color: win.cAccent2
                    Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 16 * u; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: win.setSearch("") }
                }
            }

            // Teclado en pantalla: se despliega debajo de la barra (Ⓧ en el mando o clic en la barra).
            // El preview de la derecha sigue visible y la lista de abajo se filtra mientras escribes.
            Rectangle {
                id: search
                anchors { top: searchBar.bottom; left: parent.left; right: parent.right; margins: 8 * u }
                height: win.searchOpen ? keyCol.height + 16 * u : 0
                visible: win.searchOpen
                clip: true
                color: "#a0000020"; border.color: win.cAccent; border.width: 2 * u; radius: 4 * u
                property int index: 0
                readonly property int cols: 10
                readonly property var keys: ["A","B","C","D","E","F","G","H","I","J",
                                             "K","L","M","N","O","P","Q","R","S","T",
                                             "U","V","W","X","Y","Z","0","1","2","3",
                                             "4","5","6","7","8","9","ESP","BORRAR","LIMPIAR","LISTO"]
                function type(ch) { if (Games.search.length < 24) win.setSearch(Games.search + ch) }
                function backspace() { if (Games.search.length > 0) win.setSearch(Games.search.slice(0, -1)) }
                function press(i) {
                    var k = keys[i]
                    if (k === "ESP") type(" ")
                    else if (k === "BORRAR") backspace()
                    else if (k === "LIMPIAR") win.setSearch("")
                    else if (k === "LISTO") win.searchOpen = false
                    else type(k)
                }
                function handle(a) {
                    var rows = keys.length / cols, r = Math.floor(index / cols), c = index % cols
                    if (a === "up") index = ((r + rows - 1) % rows) * cols + c
                    else if (a === "down") index = ((r + 1) % rows) * cols + c
                    else if (a === "left") index = r * cols + (c + cols - 1) % cols
                    else if (a === "right") index = r * cols + (c + 1) % cols
                    else if (a === "accept") press(index)
                    else if (a === "back") { if (Games.search.length > 0) backspace(); else win.searchOpen = false }
                    else if (a === "search") win.searchOpen = false
                    else if (a === "pageUp") win.move(-1)      // LB/RB recorren los resultados sin cerrar
                    else if (a === "pageDown") win.move(1)
                    else if (a === "systemPrev") win.changeSystem(-1)
                    else if (a === "systemNext") win.changeSystem(1)
                }

                Column {
                    id: keyCol
                    anchors { top: parent.top; topMargin: 8 * u; horizontalCenter: parent.horizontalCenter }
                    spacing: 6 * u
                    Grid {
                        id: keyGrid
                        anchors.horizontalCenter: parent.horizontalCenter
                        columns: search.cols; spacing: 5 * u
                        Repeater {
                            model: search.keys.length
                            Rectangle {
                                readonly property bool sel: index === search.index
                                width: (search.width - 20 * u - (search.cols - 1) * 5 * u) / search.cols; height: 36 * u; radius: 4 * u
                                color: sel ? win.cAccent : "#30ffffff"
                                border.color: sel ? "white" : "transparent"; border.width: 2 * u
                                Text {
                                    anchors.centerIn: parent
                                    text: search.keys[index]
                                    color: sel ? win.cOnAccent : win.cText
                                    font.family: arcadeFont; font.bold: true
                                    font.pixelSize: (search.keys[index].length > 1 ? 9 : 19) * u
                                }
                                MouseArea { anchors.fill: parent; onClicked: { search.index = index; search.press(index) } }
                            }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Ⓐ ESCRIBIR   Ⓑ BORRAR   LB/RB RESULTADOS   Ⓧ/□ LISTO"
                        color: win.cDim; font.family: arcadeFont; font.pixelSize: 12 * u
                    }
                }
            }
            ListView {
                id: list
                anchors { top: search.bottom; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8 * u }
                clip: true
                model: Games
                currentIndex: win.current
                highlightMoveDuration: 60
                highlightFollowsCurrentItem: true
                interactive: false // sin arrastre: la lista siempre sigue al juego seleccionado
                preferredHighlightBegin: height * 0.4
                preferredHighlightEnd: height * 0.6
                highlightRangeMode: ListView.ApplyRange
                highlight: Rectangle {
                    color: win.cAccent; radius: 3 * u
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.7; duration: 450 }
                        NumberAnimation { to: 1.0; duration: 450 }
                    }
                }
                delegate: Item {
                    width: ListView.view.width; height: 34 * u
                    readonly property bool sel: index === win.current
                    // Mouse: clic selecciona, doble clic juega, la rueda recorre la lista
                    MouseArea {
                        anchors.fill: parent
                        onClicked: win.select(index)
                        onDoubleClicked: { win.select(index); win.act("accept") }
                        onWheel: (w) => win.move(w.angleDelta.y > 0 ? -1 : 1)
                    }
                    Text {
                        id: num
                        anchors.verticalCenter: parent.verticalCenter
                        x: 10 * u; width: 64 * u
                        text: ("000" + (index + 1)).slice(-3)
                        font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
                        color: sel ? win.cOnAccent : win.cAccent2
                    }
                    // ✔ funciona · ✘ no funciona · nada = aún sin probar
                    Text {
                        id: mark
                        anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 10 * u }
                        width: 26 * u; horizontalAlignment: Text.AlignHCenter
                        text: status > 0 ? "✔" : status < 0 ? "✘" : ""
                        font.pixelSize: 20 * u; font.bold: true
                        color: status > 0 ? (sel ? "#006020" : "#40e070") : (sel ? "#900000" : "#ff4050")
                    }
                    Text {
                        id: star
                        anchors { verticalCenter: parent.verticalCenter; right: mark.left }
                        width: favorite ? 24 * u : 0; horizontalAlignment: Text.AlignHCenter
                        text: favorite ? "★" : ""
                        font.pixelSize: 19 * u
                        color: sel ? win.cOnAccent : "#ffd040"
                    }
                    Text {
                        anchors { verticalCenter: parent.verticalCenter; left: num.right; right: star.left; rightMargin: 6 * u }
                        text: title
                        elide: Text.ElideRight
                        font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: sel
                        color: sel ? win.cOnAccent : win.cText
                    }
                }
            }
            Text {
                anchors.centerIn: parent; visible: Games.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: Games.total === 0 ? "NO HAY JUEGOS\n\nCopia tus archivos .zip en:\n" + App.baseDir + "/roms"
                      : Games.search !== "" ? "SIN RESULTADOS\n\nPrueba con otra palabra\no cambia de sistema"
                      : Games.system.indexOf("FAVORITOS") >= 0 ? "AÚN NO TIENES FAVORITOS\n\nElige un juego y pulsa Ⓨ/△ o F2\npara añadirlo aquí"
                      : Games.system === "RECIENTES" ? "AÚN NO HAS JUGADO NADA\n\nAquí aparecerán los últimos\njuegos que abras"
                      : home.folderOf(Games.system) !== "" ? "ESTE SISTEMA AÚN NO TIENE JUEGOS\n\nCopia los juegos en:\n" + App.baseDir + "/roms/" + home.folderOf(Games.system)
                      : "SIN RESULTADOS\n\nPrueba con otra palabra\no cambia de sistema"
                font.family: arcadeFont; font.pixelSize: 18 * u; color: win.cDim; wrapMode: Text.WrapAnywhere
                width: parent.width - 40 * u
            }
        }

        // ---------------- Preview ----------------
        Item {
            id: previewArea
            visible: !win.homeOpen
            anchors { top: header.bottom; left: listPanel.right; right: parent.right; bottom: footer.top; margins: 20 * u }

            Rectangle {
                id: screenFrame
                anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                width: Math.max(0, Math.min(parent.width, (parent.height - info.height - 16 * u) * 4 / 3))
                height: width * 3 / 4
                color: "black"; border.color: win.cAccent; border.width: 4 * u; radius: 4 * u

                MouseArea { anchors.fill: parent; onDoubleClicked: win.act("accept") } // doble clic en el preview = jugar

                Image {
                    id: snap
                    anchors.fill: parent; anchors.margins: 4 * u
                    source: win.game.image || ""
                    fillMode: Image.PreserveAspectFit
                    smooth: false
                    visible: !video.visible
                }
                Video {
                    id: video
                    anchors.fill: parent; anchors.margins: 4 * u
                    fillMode: VideoOutput.PreserveAspectFit
                    loops: MediaPlayer.Infinite
                    muted: false
                    volume: 0.6
                    visible: source.toString() !== "" && playbackState === MediaPlayer.PlayingState
                }
                Text {
                    anchors.centerIn: parent
                    visible: !snap.visible ? false : snap.status !== Image.Ready
                    text: "NO PREVIEW"
                    font.family: arcadeFont; font.pixelSize: 28 * u; font.bold: true
                    color: win.cDim
                }
                // Scanlines decorativas sobre la preview
                Column {
                    anchors.fill: parent; anchors.margins: 4 * u; clip: true
                    visible: App.scanlines
                    Repeater {
                        model: Math.max(0, Math.ceil(screenFrame.height / 3))
                        Rectangle { width: screenFrame.width; height: 3; color: "transparent"
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#40000000" } }
                    }
                }
            }

            Rectangle {
                anchors { bottom: screenFrame.bottom; horizontalCenter: screenFrame.horizontalCenter; bottomMargin: 14 * u }
                visible: win.attractOn
                width: attractText.width + 36 * u; height: 44 * u; radius: 6 * u
                color: "#d0000000"; border.color: win.cAccent; border.width: 2 * u
                Text {
                    id: attractText
                    anchors.centerIn: parent
                    text: "DEMOSTRACIÓN  ·  PULSA UN BOTÓN"
                    font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true; color: win.cAccent
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite; running: win.attractOn
                        NumberAnimation { to: 0.2; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }
            }

            // Espera un poco antes de cargar el video para que la navegación rápida sea fluida
            Timer {
                id: preview
                interval: 350
                onTriggered: {
                    video.stop()
                    video.source = win.game.video || ""
                    if (video.source.toString() !== "" && !App.gameRunning) video.play()
                }
            }
            Connections {
                target: win
                function onCurrentChanged() { video.stop(); video.source = ""; preview.restart() }
            }
            Connections {
                target: App
                function onGameRunningChanged() { if (App.gameRunning) { video.stop(); video.source = "" } }
            }

            Column {
                id: info
                anchors { top: screenFrame.bottom; topMargin: 16 * u; left: screenFrame.left; right: screenFrame.right }
                spacing: 6 * u
                Image {
                    width: parent.width; height: 70 * u
                    source: win.game.marquee || ""
                    fillMode: Image.PreserveAspectFit
                    visible: status === Image.Ready
                }
                Text {
                    width: parent.width
                    text: win.game.title || ""
                    font.family: arcadeFont; font.pixelSize: 26 * u; font.bold: true
                    color: "white"; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: [win.game.year, win.game.maker, win.game.players > 0 ? win.game.players + " JUGADORES" : ""]
                          .filter(function (s) { return s }).join("  ·  ")
                    font.family: arcadeFont; font.pixelSize: 18 * u
                    color: win.cDim; horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    visible: text !== ""
                    text: (win.game.favorite ? "★ FAVORITO" : "")
                          + (win.game.favorite && win.game.plays > 0 ? "  ·  " : "")
                          + (win.game.plays > 0 ? "JUGADO " + win.game.plays + (win.game.plays === 1 ? " VEZ" : " VECES")
                                                  + "  ·  " + win.playTimeText(win.game.playTime) : "")
                    font.family: arcadeFont; font.pixelSize: 16 * u
                    color: win.cAccent; horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // ---------------- Pantalla de sistemas (inicio) ----------------
        // Una tarjeta por lista: todos, favoritos, recientes y cada sistema, tenga juegos o no.
        // La imagen de la tarjeta sale de media/sistemas/<nombre>.png si existe; si no, un mosaico
        // con capturas de sus juegos.
        Item {
            id: home
            anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: footer.top }
            visible: win.homeOpen
            property int index: 0
            readonly property var card: win.cards.length > 0 ? win.cards[Math.min(index, win.cards.length - 1)] : null
            function folderOf(system) {
                for (var k = 0; k < win.cards.length; ++k) if (win.cards[k].id === system) return win.cards[k].folder
                return ""
            }
            function handle(a) {
                var n = win.cards.length
                if (n === 0) return
                if (a === "left" || a === "systemPrev") index = (index + n - 1) % n
                else if (a === "right" || a === "systemNext") index = (index + 1) % n
                else if (a === "pageUp") index = Math.max(0, index - 5)
                else if (a === "pageDown") index = Math.min(n - 1, index + 5)
                else if (a === "accept") win.enterSystem(card.id)
                else if (a === "back") win.optionsOpen = true
                else if (a === "search") { win.enterSystem(""); search.index = 0; win.searchOpen = true }
            }

            Rectangle { anchors.fill: parent; color: "#b4000008" } // oscurece el fondo del tema para que se lea
            // Fondo: una captura del sistema elegido, grande y oscurecida
            Image {
                anchors.fill: parent
                source: home.card && home.card.images.length > 0 ? home.card.images[0] : ""
                fillMode: Image.PreserveAspectCrop; smooth: true; asynchronous: true
                opacity: 0.22
            }

            ListView {
                id: cardRow
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; verticalCenterOffset: -50 * u }
                height: 360 * u
                orientation: ListView.Horizontal
                model: win.cards.length
                currentIndex: home.index
                interactive: false
                spacing: 26 * u
                highlightMoveDuration: 180
                preferredHighlightBegin: width / 2 - 140 * u
                preferredHighlightEnd: width / 2 + 140 * u
                highlightRangeMode: ListView.StrictlyEnforceRange
                delegate: Item {
                    readonly property var c: win.cards[index]
                    readonly property bool sel: index === home.index
                    width: 280 * u; height: cardRow.height
                    Rectangle {
                        anchors.centerIn: parent
                        width: 280 * u; height: 320 * u; radius: 10 * u
                        scale: sel ? 1.1 : 0.9
                        opacity: sel ? 1 : (c && c.count > 0 ? 0.75 : 0.45)
                        Behavior on scale { NumberAnimation { duration: 140 } }
                        color: win.cPanel
                        border.color: sel ? win.cAccent : win.cBorder; border.width: (sel ? 4 : 2) * u
                        // Imagen de la tarjeta
                        Rectangle {
                            id: art
                            anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 * u }
                            height: 200 * u; color: "black"; clip: true; radius: 4 * u
                            Image {
                                anchors.fill: parent
                                visible: c && c.logo !== ""
                                source: c ? c.logo : ""
                                fillMode: Image.PreserveAspectFit; smooth: true; asynchronous: true
                            }
                            Grid { // mosaico 2x2 con capturas de sus juegos
                                anchors.fill: parent
                                visible: c && c.logo === "" && c.images.length > 0
                                columns: c && c.images.length > 1 ? 2 : 1
                                Repeater {
                                    model: c && c.logo === "" ? c.images.length : 0
                                    Image {
                                        width: art.width / (c.images.length > 1 ? 2 : 1)
                                        height: art.height / (c.images.length > 2 ? 2 : 1)
                                        source: c.images[index]
                                        fillMode: Image.PreserveAspectCrop; smooth: false; asynchronous: true
                                        sourceSize.width: 320
                                    }
                                }
                            }
                            Text { // sin imagen ni juegos: iniciales grandes
                                anchors.centerIn: parent
                                visible: c && c.logo === "" && c.images.length === 0
                                text: c ? c.name.split(/[ \/]+/).map(function (w) { return w.charAt(0) }).join("").substring(0, 3) : ""
                                color: win.cDim; font.family: arcadeFont; font.pixelSize: 80 * u; font.bold: true
                            }
                        }
                        Text {
                            anchors { top: art.bottom; topMargin: 10 * u; left: parent.left; right: parent.right; margins: 8 * u }
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            text: c ? c.name : ""
                            color: sel ? win.cAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 22 * u; font.bold: true
                        }
                        Text {
                            anchors { bottom: parent.bottom; bottomMargin: 12 * u; horizontalCenter: parent.horizontalCenter }
                            text: !c ? "" : c.count > 0 ? c.count + (c.count === 1 ? " JUEGO" : " JUEGOS") : "SIN JUEGOS AÚN"
                            color: c && c.count > 0 ? win.cAccent2 : win.cDim
                            font.family: arcadeFont; font.pixelSize: 17 * u; font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { if (sel) win.enterSystem(c.id); else home.index = index }
                            onDoubleClicked: { home.index = index; win.enterSystem(c.id) }
                            onWheel: (w) => home.handle(w.angleDelta.y > 0 ? "left" : "right")
                        }
                    }
                }
            }
            Column {
                anchors { top: cardRow.bottom; topMargin: 8 * u; horizontalCenter: parent.horizontalCenter }
                spacing: 6 * u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: home.card ? home.card.name : ""
                    color: "white"; style: Text.Outline; styleColor: "black"
                    font.family: arcadeFont; font.pixelSize: 40 * u; font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: !home.card ? "" : home.card.count > 0 ? "Ⓐ / ENTER PARA ENTRAR"
                          : home.card.folder !== "" ? "COPIA SUS JUEGOS EN  roms\\" + home.card.folder
                          : home.card.id.indexOf("FAVORITOS") >= 0 ? "MARCA JUEGOS CON Ⓨ/△ O F2 PARA VERLOS AQUÍ" : "AQUÍ SALDRÁN LOS ÚLTIMOS JUEGOS QUE ABRAS"
                    color: win.cDim; font.family: arcadeFont; font.pixelSize: 18 * u
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (home.index + 1) + " / " + win.cards.length
                    color: win.cDim; font.family: arcadeFont; font.pixelSize: 14 * u
                }
            }
        }

        // ---------------- Pie con controles ----------------
        Rectangle {
            id: footer
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 48 * u
            color: "#000010"
            Rectangle { anchors.top: parent.top; width: parent.width; height: 2 * u; color: win.cAccent2 }
            // Ayuda de controles; las que tienen acción también son botones para el mouse
            Row {
                anchors.verticalCenter: parent.verticalCenter; x: 24 * u
                spacing: 20 * u
                Repeater {
                    model: win.homeOpen
                           ? [ { t: "◄► ELEGIR SISTEMA", a: "" }, { t: "Ⓐ/✕ ENTRAR", a: "accept" },
                               { t: "Ⓧ/□ BUSCAR EN TODOS", a: "search" }, { t: "Ⓑ/○ OPCIONES", a: "back" } ]
                           : [ { t: "▲▼ ELEGIR", a: "" }, { t: "◄► SALTAR 10", a: "" }, { t: "LB/RB LETRA", a: "" },
                               { t: "Ⓐ/✕ JUGAR", a: "accept" }, { t: "Ⓑ/○ SISTEMAS", a: "back" },
                               { t: "Ⓧ/□ BUSCAR", a: "search" }, { t: "Ⓨ/△ FAVORITO", a: "favorite" },
                               { t: "LT/RT LISTA", a: "systemNext" }, { t: "F1 OPCIONES", a: "options" } ]
                    Text {
                        text: modelData.t
                        font.family: arcadeFont; font.pixelSize: 15 * u; color: win.cText
                        MouseArea {
                            anchors.fill: parent; anchors.margins: -8 * u
                            enabled: modelData.a !== ""
                            onClicked: win.act(modelData.a)
                        }
                    }
                }
            }
            Text {
                id: pressStart
                anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 24 * u }
                text: Pad.connectedCount > 0 ? "PRESS START" : "INSERT COIN"
                font.family: arcadeFont; font.pixelSize: 18 * u; font.bold: true; color: win.cAccent
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { to: 0; duration: 500 }
                    NumberAnimation { to: 1; duration: 500 }
                }
            }
        }
    }

    // =======================================================================
    //  JUEGO
    // =======================================================================
    EmulatorView {
        id: emu
        anchors.fill: parent
        visible: App.gameRunning
        scanlines: App.scanlines
        smooth: App.smooth
        aspectMode: App.aspectMode
        crt: App.crt

        // ---------------- Marco alrededor del juego ----------------
        // Imagen propia (media/bezels/<rom>.png o default.png, con el hueco transparente) o,
        // si no hay, un marco sencillo con los colores del tema que rellena las franjas negras.
        Image {
            anchors.fill: parent
            visible: App.bezel && App.bezelImage !== "" && App.aspectMode !== 2
            source: App.bezel ? App.bezelImage : ""
            fillMode: Image.Stretch; smooth: true; asynchronous: true
        }
        Item {
            id: plainBezel
            anchors.fill: parent
            visible: App.bezel && App.bezelImage === "" && r.width > 0
            readonly property rect r: emu.contentRect
            Repeater { // izquierda, derecha, arriba, abajo
                model: [ Qt.rect(0, 0, plainBezel.r.x, emu.height),
                         Qt.rect(plainBezel.r.x + plainBezel.r.width, 0, emu.width - plainBezel.r.x - plainBezel.r.width, emu.height),
                         Qt.rect(plainBezel.r.x, 0, plainBezel.r.width, plainBezel.r.y),
                         Qt.rect(plainBezel.r.x, plainBezel.r.y + plainBezel.r.height, plainBezel.r.width,
                                 emu.height - plainBezel.r.y - plainBezel.r.height) ]
                Rectangle {
                    x: modelData.x; y: modelData.y; width: Math.max(0, modelData.width); height: Math.max(0, modelData.height)
                    gradient: Gradient {
                        GradientStop { position: 0; color: win.cBg1 }
                        GradientStop { position: 0.5; color: Qt.darker(win.cBg2, 1.4) }
                        GradientStop { position: 1; color: win.cBg1 }
                    }
                }
            }
            Rectangle { // moldura alrededor de la pantalla
                x: plainBezel.r.x - border.width; y: plainBezel.r.y - border.width
                width: plainBezel.r.width + 2 * border.width; height: plainBezel.r.height + 2 * border.width
                color: "transparent"; radius: 6 * u
                border.color: win.cAccent2; border.width: 4 * u
            }
            Rectangle {
                x: plainBezel.r.x - 9 * u; y: plainBezel.r.y - 9 * u
                width: plainBezel.r.width + 18 * u; height: plainBezel.r.height + 18 * u
                color: "transparent"; radius: 10 * u
                border.color: "#80000000"; border.width: 5 * u
            }
        }

        Keys.onPressed: (e) => {
            if (App.confirmingExit) {
                if (e.key === Qt.Key_Left || e.key === Qt.Key_Right) win.act("left")
                else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Z) win.act("accept")
                else if (e.key === Qt.Key_Escape || e.key === Qt.Key_X || e.key === Qt.Key_N) win.act("back")
                else if (e.key === Qt.Key_S || e.key === Qt.Key_Y) App.answerExit(true)
                e.accepted = true
                return
            }
            if (App.paused) { // menú de pausa con teclado
                if (e.key === Qt.Key_Up) win.act("up")
                else if (e.key === Qt.Key_Down) win.act("down")
                else if (e.key === Qt.Key_Left) win.act("left")
                else if (e.key === Qt.Key_Right) win.act("right")
                else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) win.act("accept")
                else if (e.key === Qt.Key_Escape || e.key === Qt.Key_Backspace) win.act("back")
                else if (e.key === Qt.Key_P || e.key === Qt.Key_Pause) win.act("pause")
                e.accepted = true
                return
            }
            switch (e.key) {
            case Qt.Key_F1:  App.resetGame(); break
            case Qt.Key_F2:  App.scanlines = !App.scanlines; toast.show(App.scanlines ? "Scanlines ON" : "Scanlines OFF"); break
            case Qt.Key_F3:  App.smooth = !App.smooth; toast.show(App.smooth ? "Filtro suave ON" : "Pixeles nítidos"); break
            case Qt.Key_F4:  App.fastForward = !App.fastForward; break
            case Qt.Key_F5:  App.saveState(0); break
            case Qt.Key_F7:  App.loadState(0); break
            case Qt.Key_F8:  App.aspectMode = App.aspectMode + 1; toast.show("IMAGEN: " + win.aspectNames[App.aspectMode]); break
            case Qt.Key_F9:  App.volume = App.volume - 10; toast.show("VOLUMEN " + App.volume + " %"); break
            case Qt.Key_F10: App.volume = App.volume + 10; toast.show("VOLUMEN " + App.volume + " %"); break
            case Qt.Key_F12: App.takeScreenshot(); break
            case Qt.Key_F11: App.fullscreen = !App.fullscreen; break
            default: return
            }
            e.accepted = true
        }

        // ---------------- Pausa (clic del stick derecho / tecla P) ----------------
        // ---------------- Aviso antes de salir del juego ----------------
        // El juego queda congelado; por defecto está marcado "NO" para no salir por accidente.
        Rectangle {
            id: exitDlg
            anchors.fill: parent
            visible: App.confirmingExit
            color: "#c8000000"
            z: 10
            property int index: 1 // 0 = sí, 1 = no
            onVisibleChanged: if (visible) index = 1
            function handle(a) {
                if (a === "left" || a === "right" || a === "up" || a === "down") index = 1 - index
                else if (a === "accept") App.answerExit(index === 0)
                else if (a === "back") App.answerExit(false)
            }
            MouseArea { anchors.fill: parent; onWheel: {} }
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 80 * u, 760 * u); height: exitCol.height + 70 * u
                color: win.cBg1; border.color: win.cAccent2; border.width: 3 * u; radius: 8 * u
                Column {
                    id: exitCol
                    anchors.centerIn: parent
                    width: parent.width - 60 * u
                    spacing: 16 * u
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: "¿SALIR DEL JUEGO?"; color: win.cAccent
                        font.family: arcadeFont; font.pixelSize: 38 * u; font.bold: true
                    }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: App.currentTitle; color: "white"; elide: Text.ElideRight
                        font.family: arcadeFont; font.pixelSize: 22 * u; font.bold: true
                    }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        text: "Volverás al menú y se perderá el avance que no hayas guardado."
                        color: win.cDim; font.family: arcadeFont; font.pixelSize: 17 * u
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 30 * u; topPadding: 8 * u
                        Repeater {
                            model: ["SÍ, SALIR", "NO, SEGUIR JUGANDO"]
                            Rectangle {
                                readonly property bool sel: index === exitDlg.index
                                width: 290 * u; height: 54 * u; radius: 6 * u
                                color: sel ? win.cAccent : "transparent"
                                border.color: sel ? "white" : win.cDim; border.width: 2 * u
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData; color: sel ? win.cOnAccent : win.cText
                                    font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
                                }
                                MouseArea { anchors.fill: parent; onClicked: App.answerExit(index === 0) }
                            }
                        }
                    }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: "◄► ELEGIR    Ⓐ / ENTER ACEPTAR    Ⓑ / ESC SEGUIR JUGANDO"
                        color: win.cDim; font.family: arcadeFont; font.pixelSize: 14 * u
                    }
                }
            }
        }

        // Aviso fijo mientras el avance rápido está activo
        Text {
            anchors { top: parent.top; right: parent.right; margins: 20 * u }
            visible: (App.fastForward || App.rewinding) && !App.paused
            text: App.rewinding ? "◄◄ REBOBINANDO" : "►► AVANCE RÁPIDO"
            font.family: arcadeFont; font.pixelSize: 22 * u; font.bold: true
            color: win.cAccent; style: Text.Outline; styleColor: "black"
        }

        // ---------------- Menú de pausa ----------------
        Rectangle {
            id: pauseMenu
            anchors.fill: parent
            visible: App.paused && !App.confirmingExit
            color: "#b0000000"
            property int index: 0
            property string panel: "" // "" = menú, "save" / "load" = ranuras, "core" = opciones del emulador
            property bool settings: false // false = menú principal de pausa, true = AJUSTES
            onVisibleChanged: if (visible) { index = 0; panel = ""; settings = false }
            function open(s) { settings = s; index = 0 }
            readonly property var mainItems: [
                { label: "CONTINUAR", act: function () { App.togglePause() } },
                { label: "GUARDAR PARTIDA", act: function () { slots.index = 0; pauseMenu.panel = "save" } },
                { label: "CARGAR PARTIDA", act: function () { slots.index = 0; pauseMenu.panel = "load" } },
                { label: "TRUCOS", act: function () { coreOpts.index = 0; pauseMenu.panel = "cheats" } },
                { label: "AJUSTES  ►", act: function () { pauseMenu.open(true) } },
                { label: "CAPTURAR PANTALLA", act: function () { App.takeScreenshot() } },
                { label: "REINICIAR JUEGO", act: function () { App.resetGame(); App.togglePause() } },
                { label: "SALIR DEL JUEGO", act: function () { App.stopGame() } }
            ].concat(App.diskCount > 1 ? [{ label: "CAMBIAR DE DISCO (" + App.diskCount + ")", act: function () { App.nextDisk() } }] : [])
            // Con "SOLO PARA ESTE JUEGO" en SÍ, imagen y controles se guardan aparte para este juego
            readonly property var settingsItems: [
                { label: "SOLO PARA ESTE JUEGO: " + (App.gameConfig ? "SÍ" : "NO"), act: function () {
                      App.gameConfig = !App.gameConfig
                      toast.show(App.gameConfig ? "IMAGEN Y CONTROLES SE GUARDAN SOLO PARA ESTE JUEGO"
                                                : "ESTE JUEGO VUELVE A USAR LOS AJUSTES GENERALES") } },
                { label: "◄ TAMAÑO: " + win.aspectNames[App.aspectMode] + " ►", act: function () { App.aspectMode = App.aspectMode + 1 },
                  side: function (d) { App.aspectMode = App.aspectMode + d } },
                { label: "◄ EFECTO CRT: " + win.crtNames[App.crt] + " ►", act: function () { App.crt = App.crt + 1 },
                  side: function (d) { App.crt = App.crt + d } },
                { label: "SCANLINES SIMPLES: " + (App.scanlines ? "SÍ" : "NO"), act: function () { App.scanlines = !App.scanlines } },
                { label: "MARCO: " + (App.bezel ? "SÍ" : "NO"), act: function () { App.bezel = !App.bezel } },
                { label: "CONTROLES", act: function () { remap.index = 0; remap.dev = 0; win.remapOpen = true } },
                { label: "◄ VOLUMEN: " + App.volume + " % ►", act: function () { App.volume = App.volume >= 100 ? 0 : App.volume + 10 },
                  side: function (d) { App.volume = App.volume + d * 10 } },
                { label: "AVANCE RÁPIDO: " + (App.fastForward ? "SÍ" : "NO"), act: function () { App.fastForward = !App.fastForward } },
                { label: "OPCIONES DEL EMULADOR", act: function () { coreOpts.index = 0; pauseMenu.panel = "core" } },
                { label: "VOLVER", act: function () { pauseMenu.open(false) } }
            ]
            readonly property var items: settings ? settingsItems : mainItems
            function handle(a) {
                if (a === "pause") { App.togglePause(); return }
                if (panel === "core" || panel === "cheats") { coreOpts.handle(a); return }
                if (panel !== "") { slots.handle(a); return }
                if (a === "up") index = (index + items.length - 1) % items.length
                else if (a === "down") index = (index + 1) % items.length
                else if (a === "accept") items[index].act()
                else if ((a === "left" || a === "right") && items[index].side) items[index].side(a === "left" ? -1 : 1)
                else if (a === "back") { if (settings) open(false); else App.togglePause() }
            }
            MouseArea { anchors.fill: parent; onWheel: {} }
            Column {
                anchors.centerIn: parent
                visible: pauseMenu.panel === ""
                spacing: 7 * u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: pauseMenu.settings ? "AJUSTES" : "PAUSA"
                    font.family: arcadeFont; font.pixelSize: 56 * u; font.bold: true
                    color: win.cAccent; style: Text.Outline; styleColor: "#600010"
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite; running: App.paused
                        NumberAnimation { to: 0.35; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: App.currentTitle; color: "white"
                    font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
                    bottomPadding: 8 * u
                }
                Repeater {
                    model: pauseMenu.items.length
                    Rectangle {
                        readonly property bool sel: index === pauseMenu.index
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 460 * u; height: 40 * u; radius: 4 * u
                        color: sel ? win.cAccent : "#40000000"
                        border.color: sel ? "white" : "transparent"; border.width: 2 * u
                        Text {
                            anchors.centerIn: parent
                            text: pauseMenu.items[index].label
                            color: sel ? win.cOnAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: (m) => {
                                pauseMenu.index = index
                                var it = pauseMenu.items[index]
                                if (it.side) it.side(m.x < width / 2 ? -1 : 1); else it.act()
                            }
                        }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    topPadding: 6 * u
                    text: "▲▼ ELEGIR    ◄► CAMBIAR    Ⓐ / ENTER ACEPTAR    Ⓑ / " + (Pad.mapRevision, Pad.bindingName(10)) + " / P CONTINUAR"
                    font.family: arcadeFont; font.pixelSize: 15 * u; color: win.cDim
                }
            }

            // ---- Ranuras de guardado: 6 por juego, cada una con su captura y fecha ----
            Item {
                id: slots
                anchors.fill: parent
                visible: pauseMenu.panel === "save" || pauseMenu.panel === "load"
                property int index: 0
                readonly property bool saving: pauseMenu.panel === "save"
                readonly property var entries: (App.stateRev, App.gameRunning ? App.stateSlots() : [])
                function choose(i) {
                    index = i
                    if (saving) { App.saveState(i); pauseMenu.panel = "" }
                    else if (entries[i].used) { App.loadState(i); App.togglePause() }
                    else toast.show("LA RANURA " + (i + 1) + " ESTÁ VACÍA")
                }
                function handle(a) {
                    if (a === "left") index = (index + 5) % 6
                    else if (a === "right") index = (index + 1) % 6
                    else if (a === "up" || a === "down") index = (index + 3) % 6
                    else if (a === "accept") choose(index)
                    else if (a === "back") pauseMenu.panel = ""
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 14 * u
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: slots.saving ? "GUARDAR PARTIDA" : "CARGAR PARTIDA"
                        font.family: arcadeFont; font.pixelSize: 40 * u; font.bold: true
                        color: win.cAccent; style: Text.Outline; styleColor: "#600010"
                    }
                    Grid {
                        anchors.horizontalCenter: parent.horizontalCenter
                        columns: 3; spacing: 16 * u
                        Repeater {
                            model: slots.entries.length
                            Rectangle {
                                readonly property bool sel: index === slots.index
                                readonly property var slot: slots.entries[index]
                                width: 300 * u; height: 236 * u; radius: 6 * u
                                color: sel ? win.cAccent : "#c0000010"
                                border.color: sel ? "white" : win.cBorder; border.width: 2 * u
                                Rectangle {
                                    id: thumb
                                    anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 8 * u }
                                    width: 284 * u; height: 170 * u; color: "black"
                                    Image {
                                        anchors.fill: parent
                                        source: slot.image || ""
                                        fillMode: Image.PreserveAspectFit; smooth: false; cache: false
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: !slot.used
                                        text: "VACÍA"; color: win.cDim
                                        font.family: arcadeFont; font.pixelSize: 24 * u; font.bold: true
                                    }
                                }
                                Text {
                                    anchors { top: thumb.bottom; topMargin: 6 * u; horizontalCenter: parent.horizontalCenter }
                                    text: "RANURA " + (index + 1)
                                    color: sel ? win.cOnAccent : win.cText
                                    font.family: arcadeFont; font.pixelSize: 18 * u; font.bold: true
                                }
                                Text {
                                    anchors { bottom: parent.bottom; bottomMargin: 6 * u; horizontalCenter: parent.horizontalCenter }
                                    text: slot.used ? slot.when : "—"
                                    color: sel ? win.cOnAccent : win.cDim
                                    font.family: arcadeFont; font.pixelSize: 14 * u
                                }
                                MouseArea { anchors.fill: parent; onClicked: slots.choose(index) }
                            }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "◄►▲▼ ELEGIR    Ⓐ / ENTER " + (slots.saving ? "GUARDAR AQUÍ" : "CARGAR") + "    Ⓑ / ESC VOLVER"
                        font.family: arcadeFont; font.pixelSize: 15 * u; color: win.cDim
                    }
                }
            }

            // ---- Opciones del emulador: lo que el núcleo permite cambiar (dificultad, región, DIP…) ----
            Item {
                id: coreOpts
                anchors.fill: parent
                visible: pauseMenu.panel === "core" || pauseMenu.panel === "cheats"
                property int index: 0
                // FBNeo publica los trucos (system/fbneo/cheats/<rom>.ini) como opciones "[Cheat][rom.ini] Nombre":
                // van en su propio panel y no se mezclan con las opciones normales
                readonly property bool cheats: pauseMenu.panel === "cheats"
                readonly property var all: (App.optionsRev, App.gameRunning ? App.coreOptions() : [])
                readonly property var entries: all.filter(function (o) { return (o.label.indexOf("[Cheat]") === 0) === cheats })
                function title(o) { return cheats ? o.label.replace(/^\[Cheat\](\[[^\]]*\])?\s*/, "") : o.label }
                function step(d) { if (entries.length > 0) App.stepCoreOption(entries[index].key, d) }
                function handle(a) {
                    var n = entries.length
                    if (a === "back") pauseMenu.panel = ""
                    else if (n === 0) return
                    else if (a === "up") index = (index + n - 1) % n
                    else if (a === "down") index = (index + 1) % n
                    else if (a === "pageUp") index = Math.max(0, index - 10)
                    else if (a === "pageDown") index = Math.min(n - 1, index + 10)
                    else if (a === "left") step(-1)
                    else if (a === "right" || a === "accept") step(1)
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 60 * u, 980 * u); height: parent.height - 80 * u
                    color: win.cBg1; border.color: win.cAccent; border.width: 3 * u; radius: 8 * u
                    Text {
                        id: coreTitle
                        anchors { top: parent.top; topMargin: 16 * u; horizontalCenter: parent.horizontalCenter }
                        text: coreOpts.cheats ? "TRUCOS" : "OPCIONES DEL EMULADOR"; color: win.cAccent2
                        font.family: arcadeFont; font.pixelSize: 28 * u; font.bold: true
                    }
                    ListView {
                        id: coreList
                        anchors { top: coreTitle.bottom; bottom: coreHelp.top; left: parent.left; right: parent.right; margins: 14 * u }
                        clip: true; interactive: false
                        model: coreOpts.entries.length
                        currentIndex: coreOpts.index
                        highlightMoveDuration: 0
                        delegate: Rectangle {
                            readonly property bool sel: index === coreOpts.index
                            readonly property var opt: coreOpts.entries[index]
                            width: ListView.view.width; height: 34 * u; radius: 4 * u
                            color: sel ? win.cAccent : "transparent"
                            Text {
                                anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 12 * u; right: val.left; rightMargin: 10 * u }
                                text: opt ? coreOpts.title(opt) : ""; elide: Text.ElideRight
                                color: sel ? win.cOnAccent : win.cText
                                font.family: arcadeFont; font.pixelSize: 17 * u; font.bold: sel
                            }
                            Text {
                                id: val
                                anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 12 * u }
                                width: Math.min(implicitWidth, parent.width * 0.5); elide: Text.ElideRight
                                text: opt ? "◄ " + opt.value + " ►" : ""
                                color: sel ? win.cOnAccent : win.cAccent
                                font.family: arcadeFont; font.pixelSize: 17 * u; font.bold: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: (m) => { coreOpts.index = index; coreOpts.step(m.x < width * 0.75 ? 1 : (m.x < width * 0.875 ? -1 : 1)) }
                                onWheel: (w) => coreOpts.handle(w.angleDelta.y > 0 ? "up" : "down")
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: coreOpts.entries.length === 0
                        text: coreOpts.cheats ? "NO HAY TRUCOS PARA ESTE JUEGO" : "ESTE EMULADOR NO TIENE OPCIONES"; color: win.cDim
                        font.family: arcadeFont; font.pixelSize: 20 * u
                    }
                    Text {
                        id: coreHelp
                        anchors { bottom: parent.bottom; bottomMargin: 12 * u; horizontalCenter: parent.horizontalCenter }
                        horizontalAlignment: Text.AlignHCenter
                        text: "▲▼ ELEGIR    ◄► CAMBIAR    LB/RB SALTAR 10    Ⓑ / ESC VOLVER\n" + (coreOpts.cheats ? "LOS TRUCOS SE APAGAN AL CERRAR EL ARCADE"
                                                 : "SE GUARDAN SOLAS · ALGUNAS SOLO SE APLICAN AL REINICIAR O VOLVER A ABRIR EL JUEGO")
                        font.family: arcadeFont; font.pixelSize: 14 * u; color: win.cDim
                    }
                }
            }
        }
    }

    // =======================================================================
    //  OPCIONES (Ⓑ / Esc en el menú)
    // =======================================================================
    Rectangle {
        id: options
        anchors.fill: parent
        color: "#90000000" // deja ver el menú detrás para apreciar el tema y el fondo al cambiarlos
        visible: win.optionsOpen && !win.remapOpen && !App.gameRunning
        property int index: 0
        property string section: "" // "" = menú principal de opciones
        onVisibleChanged: if (visible && !win.remapOpen) { section = ""; index = 0 }
        function open(sec) { section = sec; index = 0 }
        function yn(v) { return v ? "SÍ" : "NO" }
        // "side" = lo que hacen ◄ ► en esa fila (anterior / siguiente)
        readonly property var sections: ({
            "": { title: "OPCIONES", items: [
                { label: "CONTINUAR", act: function () { win.optionsOpen = false } },
                { label: "IMAGEN  ►", act: function () { options.open("imagen") } },
                { label: "SONIDO  ►", act: function () { options.open("sonido") } },
                { label: "JUEGO  ►", act: function () { options.open("juego") } },
                { label: "LISTA DE JUEGOS  ►", act: function () { options.open("lista") } },
                { label: "APARIENCIA DEL MENÚ  ►", act: function () { options.open("menu") } },
                { label: "CONFIGURAR CONTROLES", act: function () { remap.index = 0; remap.dev = 0; win.remapOpen = true } },
                { label: "SALIR DEL ARCADE", act: function () { App.quit() } } ] },
            "imagen": { title: "IMAGEN", items: [
                { label: "◄ EFECTO CRT: " + win.crtNames[App.crt] + " ►", act: function () { App.crt = App.crt + 1 }, side: function (d) { App.crt = App.crt + d } },
                { label: "SCANLINES SIMPLES: " + yn(App.scanlines), act: function () { App.scanlines = !App.scanlines } },
                { label: "FILTRO SUAVE: " + yn(App.smooth), act: function () { App.smooth = !App.smooth } },
                { label: "◄ TAMAÑO: " + win.aspectNames[App.aspectMode] + " ►", act: function () { App.aspectMode = App.aspectMode + 1 }, side: function (d) { App.aspectMode = App.aspectMode + d } },
                { label: "MARCO DEL JUEGO: " + yn(App.bezel), act: function () { App.bezel = !App.bezel } },
                { label: "PANTALLA COMPLETA: " + yn(App.fullscreen), act: function () { App.fullscreen = !App.fullscreen } } ] },
            "sonido": { title: "SONIDO", items: [
                { label: "◄ VOLUMEN: " + App.volume + " % ►", act: function () { App.volume = App.volume >= 100 ? 0 : App.volume + 10 }, side: function (d) { App.volume = App.volume + d * 10 } },
                { label: "SONIDOS DEL MENÚ: " + yn(App.menuSounds), act: function () { App.menuSounds = !App.menuSounds } },
                { label: "MÚSICA DEL MENÚ: " + (App.musicUrl === "" ? "SIN ARCHIVO" : yn(App.menuMusic)), act: function () {
                      if (App.musicUrl === "") toast.show("PON TU MÚSICA EN sounds\\musica.mp3 Y REINICIA EL ARCADE")
                      else App.menuMusic = !App.menuMusic } } ] },
            "juego": { title: "JUEGO", items: [
                { label: "CONTINUAR DONDE LO DEJÉ: " + yn(App.autoResume), act: function () { App.autoResume = !App.autoResume } },
                { label: "REBOBINAR (RETROCESO): " + yn(App.rewind), act: function () { App.rewind = !App.rewind } } ] },
            "lista": { title: "LISTA DE JUEGOS", items: [
                { label: "OCULTAR JUEGOS CON ✘: " + yn(App.hideBroken), act: function () { win.refilter(function () { App.hideBroken = !App.hideBroken }) } },
                { label: "OCULTAR VERSIONES REPETIDAS: " + yn(App.hideClones), act: function () { win.refilter(function () { App.hideClones = !App.hideClones }) } },
                { label: "RECARGAR JUEGOS, TEMAS Y FONDOS", act: function () { Games.rescan(); Theme.reload(); win.current = 0; win.optionsOpen = false } } ] },
            "menu": { title: "APARIENCIA DEL MENÚ", items: [
                { label: "◄ TEMA: " + Theme.name + " ►", act: function () { Theme.nextTheme(1) }, side: function (d) { Theme.nextTheme(d) } },
                { label: "◄ FONDO: " + Theme.backgroundName.substring(0, 24) + " ►", act: function () { Theme.nextBackground(1) }, side: function (d) { Theme.nextBackground(d) } },
                { label: "MODO ATRACCIÓN: " + yn(App.attract), act: function () { App.attract = !App.attract } } ] }
        })
        readonly property var items: sections[section].items.concat(
            section === "" ? [] : [{ label: "VOLVER", act: function () { options.open("") } }])
        function handle(a) {
            if (a === "up") index = (index + items.length - 1) % items.length
            else if (a === "down") index = (index + 1) % items.length
            else if (a === "accept") items[index].act()
            else if ((a === "left" || a === "right") && items[index].side) items[index].side(a === "left" ? -1 : 1)
            else if (a === "back") { if (section === "") win.optionsOpen = false; else open("") }
        }
        MouseArea { anchors.fill: parent; onClicked: win.optionsOpen = false; onWheel: {} } // clic fuera = cerrar
        Rectangle {
            anchors.centerIn: parent
            width: 560 * u; height: col.height + 60 * u
            color: win.cBg1; border.color: win.cAccent; border.width: 3 * u; radius: 8 * u
            MouseArea { anchors.fill: parent }
            Column {
                id: col
                anchors.centerIn: parent
                spacing: 8 * u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: options.sections[options.section].title; color: win.cAccent2
                    font.family: arcadeFont; font.pixelSize: 30 * u; font.bold: true
                    bottomPadding: 4 * u
                }
                Repeater {
                    model: options.items.length
                    Rectangle {
                        readonly property var item: options.items[index]
                        width: 500 * u; height: 40 * u; radius: 4 * u
                        color: index === options.index ? win.cAccent : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: item ? item.label : ""
                            color: index === options.index ? win.cOnAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: (m) => {
                                options.index = index
                                if (item.side) item.side(m.x < width / 2 ? -1 : 1); else item.act()
                            }
                        }
                    }
                }
            }
        }
    }

    // =======================================================================
    //  CONTROLES (Opciones → Configurar controles)
    //  Cada fila es una acción del juego; al aceptar espera el botón del mando que la hará.
    // =======================================================================
    Rectangle {
        id: remap
        anchors.fill: parent
        color: "#c0000000"
        visible: win.remapOpen && (!App.gameRunning || App.paused)
        z: 20
        property int index: 0
        property int dev: 0 // 0..3 = mando de cada jugador, 4 y 5 = teclado de J1 y J2
        readonly property bool keyboard: dev >= 4
        readonly property int actions: Pad.rowCount(dev)
        readonly property int rows: actions + 3 // dispositivo + acciones + restablecer + volver
        function setDev(d) {
            dev = (d + Pad.deviceCount()) % Pad.deviceCount()
            index = Math.min(index, rows - 1)
        }
        function handle(a) {
            if (a === "up") index = (index + rows - 1) % rows
            else if (a === "down") index = (index + 1) % rows
            else if (a === "left") setDev(dev - 1)
            else if (a === "right") setDev(dev + 1)
            else if (a === "back") win.remapOpen = false
            else if (a === "accept") {
                if (index === 0) setDev(dev + 1)
                else if (index <= actions) { Pad.captureRow(dev, index - 1); captureTimeout.restart() }
                else if (index === actions + 1) { Pad.resetDevice(dev); toast.show("Controles restablecidos") }
                else win.remapOpen = false
            }
        }
        // Si nadie pulsa nada (p. ej. no hay mando), deja de esperar
        Timer { id: captureTimeout; interval: 6000; onTriggered: Pad.cancelCapture() }

        MouseArea { anchors.fill: parent; onClicked: if (!Pad.capturing) win.remapOpen = false; onWheel: {} }
        Rectangle {
            anchors.centerIn: parent
            width: 640 * u; height: remapCol.height + 40 * u
            color: win.cBg1; border.color: win.cAccent; border.width: 3 * u; radius: 8 * u
            MouseArea { anchors.fill: parent }
            Column {
                id: remapCol
                anchors.centerIn: parent
                spacing: 2 * u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: App.gameConfig ? "CONTROLES · SOLO " + App.currentTitle.substring(0, 22).toUpperCase() : "CONTROLES"; color: win.cAccent2
                    font.family: arcadeFont; font.pixelSize: (App.gameConfig ? 20 : 28) * u; font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: remap.keyboard ? "ELIGE UNA ACCIÓN Y PULSA LA TECLA QUE QUIERAS · ESC CANCELA"
                                         : "ELIGE UNA ACCIÓN Y PULSA EL BOTÓN DEL MANDO QUE QUIERAS\nTURBO = DISPARO AUTOMÁTICO AL MANTENER · MACRO = VARIOS BOTONES A LA VEZ"
                    color: win.cDim; font.family: arcadeFont; font.pixelSize: 13 * u
                    bottomPadding: 4 * u
                }
                Repeater {
                    model: remap.rows
                    Rectangle {
                        readonly property bool sel: index === remap.index
                        readonly property int row: index - 1 // fila de acción (la 0 es el selector de dispositivo)
                        readonly property bool isAction: index >= 1 && index <= remap.actions
                        readonly property bool waiting: sel && Pad.capturing
                        width: 580 * u; height: 22 * u; radius: 4 * u
                        color: sel ? win.cAccent : (index === 0 ? "#30ffffff" : "transparent")
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 16 * u
                            visible: isAction
                            text: isAction ? Pad.rowName(remap.dev, row) : ""
                            color: sel ? win.cOnAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 16 * u; font.bold: true
                        }
                        Text {
                            anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 16 * u }
                            visible: isAction
                            text: !isAction ? "" : waiting ? (remap.keyboard ? "PULSA UNA TECLA…" : "PULSA UN BOTÓN…")
                                                           : (Pad.mapRevision, Pad.rowBinding(remap.dev, row))
                            color: sel ? (waiting ? "#900000" : "black") : win.cAccent
                            font.family: arcadeFont; font.pixelSize: 16 * u; font.bold: true
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !isAction
                            text: index === 0 ? "◄ " + Pad.deviceName(remap.dev) + " ►"
                                  : index === remap.actions + 1 ? "RESTABLECER ESTE " + (remap.keyboard ? "TECLADO" : "MANDO") : "VOLVER"
                            color: sel ? win.cOnAccent : (index === 0 ? win.cAccent2 : win.cText)
                            font.family: arcadeFont; font.pixelSize: 16 * u; font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: (m) => {
                                if (Pad.capturing) return
                                remap.index = index
                                if (index === 0) remap.setDev(remap.dev + (m.x < width / 2 ? -1 : 1))
                                else remap.handle("accept")
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------- Error ----------------
    Rectangle {
        anchors.fill: parent; color: "#d0000000"
        visible: win.errorText !== ""
        z: 50
        MouseArea { anchors.fill: parent; onClicked: win.errorText = ""; onWheel: {} }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80 * u, 820 * u); height: errText.height + 120 * u
            color: "#200008"; border.color: win.cAccent2; border.width: 3 * u; radius: 8 * u
            Text {
                id: errText
                anchors { top: parent.top; topMargin: 36 * u; left: parent.left; right: parent.right; margins: 30 * u }
                text: win.errorText; wrapMode: Text.Wrap
                color: "white"; font.family: arcadeFont; font.pixelSize: 20 * u
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                anchors { bottom: parent.bottom; bottomMargin: 20 * u; horizontalCenter: parent.horizontalCenter }
                text: "Ⓐ / ENTER PARA CONTINUAR"; color: win.cAccent
                font.family: arcadeFont; font.pixelSize: 16 * u
            }
        }
    }

    // ---------------- Mensajes cortos ----------------
    Rectangle {
        id: toast
        anchors { top: parent.top; topMargin: 24 * u; horizontalCenter: parent.horizontalCenter }
        width: toastText.width + 40 * u; height: 44 * u; radius: 6 * u
        color: "#e0000000"; border.color: win.cAccent; border.width: 2 * u
        opacity: 0; z: 60
        function show(t) { toastText.text = t; opacity = 1; toastTimer.restart() }
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Text { id: toastText; anchors.centerIn: parent; color: "white"; font.family: arcadeFont; font.pixelSize: 18 * u }
        Timer { id: toastTimer; interval: 1800; onTriggered: toast.opacity = 0 }
    }

    Component.onCompleted: {
        // Arranca en la pantalla de sistemas, parada sobre la última lista que se abrió
        Games.system = App.lastSystem
        current = Math.max(0, Games.rowOfSource(App.lastIndex))
        select(current); preview.restart()
        goHome()
    }
}
