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
    property string errorText: ""

    // ---------------- Entrada unificada (teclado + mandos) ----------------
    function act(a) {
        if (App.confirmingExit) { exitDlg.handle(a); return }
        if (App.paused) { pauseMenu.handle(a); return }
        if (App.gameRunning) return
        if (errorText !== "") { if (a === "accept" || a === "back") errorText = ""; return }
        if (Pad.capturing) return
        if (searchOpen) { search.handle(a); return }
        if (remapOpen) { remap.handle(a); return }
        if (optionsOpen) { options.handle(a); return }
        switch (a) {
        case "up":       move(-1); break
        case "down":     move(1); break
        case "left":     move(-10); break
        case "right":    move(10); break
        case "pageUp":   select(Games.jumpLetter(current, -1)); break
        case "pageDown": select(Games.jumpLetter(current, 1)); break
        case "accept":   if (Games.count > 0) { clickSfx(); App.launch(current) } break
        case "back":     optionsOpen = true; options.index = 0; break
        case "search":     search.index = 0; searchOpen = true; break
        case "systemPrev": changeSystem(-1); break
        case "systemNext": changeSystem(1); break
        }
    }
    // Cambia el filtro conservando el juego seleccionado si sigue visible
    function refilter(change) {
        var src = Games.sourceRow(current)
        change()
        var row = Games.rowOfSource(src)
        select(row >= 0 ? row : 0)
    }
    function changeSystem(dir) {
        if (Games.systems.length < 2) { toast.show("SOLO HAY UN SISTEMA: " + (Games.systems[0] || "—")); return }
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
    function clickSfx() {}  // pon aquí un SoundEffect si quieres sonidos de menú

    Connections {
        target: Games
        function onFilterChanged() { win.listRev++ }
    }
    Connections {
        target: App
        function onMenuAction(a) { win.act(a) }
        function onError(t) { win.errorText = t }
        function onToast(t) { toast.show(t) }
        function onGameRunningChanged() {
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
        onPositionChanged: { win.mouseActive = true; mouseIdle.restart() }
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
            // Las letras y números no son atajos: en el menú escriben directo en la barra de búsqueda
            if (e.key === Qt.Key_F11) { App.fullscreen = !App.fullscreen; e.accepted = true; return }
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
                text: Games.count + (Games.count < Games.total ? " DE " + Games.total : "") + " JUEGOS"
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
                        text: "◄ " + (Games.system !== "" ? Games.system : "TODOS LOS SISTEMAS") + " ►"
                        font.family: arcadeFont; font.pixelSize: 18 * u; font.bold: true
                        color: Games.system !== "" ? win.cAccent : "white"
                    }
                    // Clic en la mitad izquierda = sistema anterior, derecha = siguiente
                    MouseArea { anchors.fill: parent; onClicked: (m) => win.act(m.x < width / 2 ? "systemPrev" : "systemNext") }
                }
            }
        }

        // ---------------- Lista de juegos ----------------
        Rectangle {
            id: listPanel
            anchors { top: header.bottom; left: parent.left; bottom: footer.top; margins: 20 * u }
            width: parent.width * 0.42
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
                        anchors { verticalCenter: parent.verticalCenter; left: num.right; right: mark.left; rightMargin: 6 * u }
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
                text: Games.total > 0 ? "SIN RESULTADOS\n\nPrueba con otra palabra\no cambia de sistema"
                                      : "NO HAY JUEGOS\n\nCopia tus archivos .zip en:\n" + App.baseDir + "/roms"
                font.family: arcadeFont; font.pixelSize: 18 * u; color: win.cDim; wrapMode: Text.WrapAnywhere
                width: parent.width - 40 * u
            }
        }

        // ---------------- Preview ----------------
        Item {
            id: previewArea
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
                    text: [win.game.year, win.game.maker].filter(function (s) { return s }).join("  ·  ")
                    font.family: arcadeFont; font.pixelSize: 18 * u
                    color: win.cDim; horizontalAlignment: Text.AlignHCenter
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
                spacing: 26 * u
                Repeater {
                    model: [ { t: "▲▼ ELEGIR", a: "" }, { t: "◄► SALTAR 10", a: "" }, { t: "LB/RB LETRA", a: "" },
                             { t: "Ⓐ/✕ JUGAR", a: "accept" }, { t: "Ⓑ/○ OPCIONES", a: "back" },
                             { t: "Ⓧ/□ BUSCAR", a: "search" }, { t: "LT/RT SISTEMA", a: "systemNext" } ]
                    Text {
                        text: modelData.t
                        font.family: arcadeFont; font.pixelSize: 16 * u; color: win.cText
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
                else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) win.act("accept")
                else if (e.key === Qt.Key_Escape || e.key === Qt.Key_P || e.key === Qt.Key_Pause) win.act("back")
                e.accepted = true
                return
            }
            switch (e.key) {
            case Qt.Key_F1:  App.resetGame(); break
            case Qt.Key_F2:  App.scanlines = !App.scanlines; toast.show(App.scanlines ? "Scanlines ON" : "Scanlines OFF"); break
            case Qt.Key_F3:  App.smooth = !App.smooth; toast.show(App.smooth ? "Filtro suave ON" : "Pixeles nítidos"); break
            case Qt.Key_F5:  App.saveState(0); break
            case Qt.Key_F7:  App.loadState(0); break
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

        // ---------------- Menú de pausa ----------------
        Rectangle {
            id: pauseMenu
            anchors.fill: parent
            visible: App.paused && !App.confirmingExit
            color: "#b0000000"
            property int index: 0
            onVisibleChanged: if (visible) index = 0
            readonly property var items: [
                { label: "CONTINUAR", act: function () { App.togglePause() } },
                { label: "GUARDAR PARTIDA", act: function () { App.saveState(0) } },
                { label: "CARGAR PARTIDA", act: function () { App.loadState(0) } },
                { label: "REINICIAR JUEGO", act: function () { App.resetGame(); App.togglePause() } },
                { label: "SALIR DEL JUEGO", act: function () { App.stopGame() } }
            ]
            function handle(a) {
                if (a === "up") index = (index + items.length - 1) % items.length
                else if (a === "down") index = (index + 1) % items.length
                else if (a === "accept") items[index].act()
                else if (a === "back" || a === "pause") App.togglePause()
            }
            MouseArea { anchors.fill: parent; onWheel: {} }
            Column {
                anchors.centerIn: parent
                spacing: 12 * u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "PAUSA"
                    font.family: arcadeFont; font.pixelSize: 72 * u; font.bold: true
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
                    bottomPadding: 10 * u
                }
                Repeater {
                    model: pauseMenu.items.length
                    Rectangle {
                        readonly property bool sel: index === pauseMenu.index
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 440 * u; height: 44 * u; radius: 4 * u
                        color: sel ? win.cAccent : "#40000000"
                        border.color: sel ? "white" : "transparent"; border.width: 2 * u
                        Text {
                            anchors.centerIn: parent
                            text: pauseMenu.items[index].label
                            color: sel ? win.cOnAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 21 * u; font.bold: true
                        }
                        MouseArea { anchors.fill: parent; onClicked: { pauseMenu.index = index; pauseMenu.items[index].act() } }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    topPadding: 8 * u
                    text: "▲▼ ELEGIR    Ⓐ / ENTER ACEPTAR    Ⓑ / " + (Pad.mapRevision, Pad.bindingName(10)) + " / P CONTINUAR"
                    font.family: arcadeFont; font.pixelSize: 15 * u; color: win.cDim
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
        // "side" = lo que hacen ◄ ► en esa fila (anterior / siguiente)
        readonly property var items: [
            { label: "CONTINUAR", act: function () { win.optionsOpen = false } },
            { label: "◄ TEMA: " + Theme.name + " ►", act: function () { Theme.nextTheme(1) }, side: function (d) { Theme.nextTheme(d) } },
            { label: "◄ FONDO: " + Theme.backgroundName.substring(0, 24) + " ►", act: function () { Theme.nextBackground(1) }, side: function (d) { Theme.nextBackground(d) } },
            { label: "SCANLINES: " + (App.scanlines ? "SÍ" : "NO"), act: function () { App.scanlines = !App.scanlines } },
            { label: "FILTRO SUAVE: " + (App.smooth ? "SÍ" : "NO"), act: function () { App.smooth = !App.smooth } },
            { label: "PANTALLA COMPLETA: " + (App.fullscreen ? "SÍ" : "NO"), act: function () { App.fullscreen = !App.fullscreen } },
            { label: "CONFIGURAR CONTROLES", act: function () { remap.index = 0; win.remapOpen = true } },
            { label: "RECARGAR JUEGOS, TEMAS Y FONDOS", act: function () { Games.rescan(); Theme.reload(); win.current = 0; win.optionsOpen = false } },
            { label: "SALIR", act: function () { App.quit() } }
        ]
        function handle(a) {
            if (a === "up") index = (index + items.length - 1) % items.length
            else if (a === "down") index = (index + 1) % items.length
            else if (a === "accept") items[index].act()
            else if ((a === "left" || a === "right") && items[index].side) items[index].side(a === "left" ? -1 : 1)
            else if (a === "back") win.optionsOpen = false
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
                spacing: 10 * u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "OPCIONES"; color: win.cAccent2
                    font.family: arcadeFont; font.pixelSize: 30 * u; font.bold: true
                }
                Repeater {
                    model: options.items.length
                    Rectangle {
                        width: 480 * u; height: 40 * u; radius: 4 * u
                        color: index === options.index ? win.cAccent : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: options.items[index].label
                            color: index === options.index ? win.cOnAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
                        }
                        MouseArea { anchors.fill: parent; onClicked: { options.index = index; options.items[index].act() } }
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
        visible: win.remapOpen && !App.gameRunning
        property int index: 0
        readonly property int actions: Pad.actionCount()
        readonly property int rows: actions + 2 // + restablecer + volver
        function handle(a) {
            if (a === "up") index = (index + rows - 1) % rows
            else if (a === "down") index = (index + 1) % rows
            else if (a === "back") win.remapOpen = false
            else if (a === "accept") {
                if (index < actions) { Pad.startCapture(index); captureTimeout.restart() }
                else if (index === actions) { Pad.resetMapping(); toast.show("Controles restablecidos") }
                else win.remapOpen = false
            }
        }
        // Si nadie pulsa nada (p. ej. no hay mando), deja de esperar
        Timer { id: captureTimeout; interval: 6000; onTriggered: Pad.cancelCapture() }

        MouseArea { anchors.fill: parent; onClicked: if (!Pad.capturing) win.remapOpen = false; onWheel: {} }
        Rectangle {
            anchors.centerIn: parent
            width: 640 * u; height: remapCol.height + 50 * u
            color: win.cBg1; border.color: win.cAccent; border.width: 3 * u; radius: 8 * u
            MouseArea { anchors.fill: parent }
            Column {
                id: remapCol
                anchors.centerIn: parent
                spacing: 4 * u
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "CONTROLES"; color: win.cAccent2
                    font.family: arcadeFont; font.pixelSize: 30 * u; font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Pad.connectedCount > 0 ? "ELIGE UNA ACCIÓN Y PULSA EL BOTÓN QUE QUIERAS" : "CONECTA UN MANDO PARA CONFIGURARLO"
                    color: win.cDim; font.family: arcadeFont; font.pixelSize: 15 * u
                    bottomPadding: 8 * u
                }
                Repeater {
                    model: remap.rows
                    Rectangle {
                        readonly property bool sel: index === remap.index
                        readonly property bool waiting: sel && Pad.capturing
                        width: 580 * u; height: 34 * u; radius: 4 * u
                        color: sel ? win.cAccent : "transparent"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 16 * u
                            visible: index < remap.actions
                            text: Pad.actionName(index)
                            color: sel ? win.cOnAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 19 * u; font.bold: true
                        }
                        Text {
                            anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 16 * u }
                            visible: index < remap.actions
                            text: waiting ? "PULSA UN BOTÓN…" : (Pad.mapRevision, Pad.bindingName(index))
                            color: sel ? (waiting ? "#900000" : "black") : win.cAccent
                            font.family: arcadeFont; font.pixelSize: 19 * u; font.bold: true
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: index >= remap.actions
                            text: index === remap.actions ? "RESTABLECER" : "VOLVER"
                            color: sel ? win.cOnAccent : win.cText
                            font.family: arcadeFont; font.pixelSize: 19 * u; font.bold: true
                        }
                        MouseArea { anchors.fill: parent; onClicked: if (!Pad.capturing) { remap.index = index; remap.handle("accept") } }
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

    Component.onCompleted: { select(current); preview.restart() }
}
