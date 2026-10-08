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
    readonly property color cBg1: "#0a0a2e"
    readonly property color cBg2: "#000000"
    readonly property color cAccent: "#ffcc00"     // barra de selección
    readonly property color cAccent2: "#ff3355"    // detalles rojos
    readonly property color cText: "#e8e8ff"
    readonly property color cDim: "#7a7aa8"
    readonly property real  u: Math.min(width / 1280, height / 720) // unidad de escala

    property int current: Math.min(App.lastIndex, Math.max(0, Games.count - 1))
    property var game: Games.count > 0 ? Games.get(current) : ({})
    property bool optionsOpen: false
    property string errorText: ""

    // ---------------- Entrada unificada (teclado + mandos) ----------------
    function act(a) {
        if (App.gameRunning) return
        if (errorText !== "") { if (a === "accept" || a === "back") errorText = ""; return }
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
        }
    }
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
        target: App
        function onMenuAction(a) { win.act(a) }
        function onError(t) { win.errorText = t }
        function onToast(t) { toast.show(t) }
        function onGameRunningChanged() {
            if (App.gameRunning) emu.forceActiveFocus()
            else { menuRoot.forceActiveFocus(); preview.restart() }
        }
    }

    // Oculta el cursor del mouse en modo arcade
    MouseArea { anchors.fill: parent; cursorShape: App.fullscreen ? Qt.BlankCursor : Qt.ArrowCursor; acceptedButtons: Qt.NoButton; z: 100 }

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
            map[Qt.Key_Q] = "pageUp"; map[Qt.Key_W] = "pageDown"
            map[Qt.Key_Return] = "accept"; map[Qt.Key_Enter] = "accept"; map[Qt.Key_Z] = "accept"; map[Qt.Key_1] = "accept"
            map[Qt.Key_Escape] = "back"; map[Qt.Key_X] = "back"; map[Qt.Key_Backspace] = "back"
            if (e.key === Qt.Key_F11) { App.fullscreen = !App.fullscreen; e.accepted = true; return }
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
        Grid {
            anchors.fill: parent; opacity: 0.06
            columns: Math.ceil(parent.width / (40 * u)) + 1
            Repeater {
                model: Math.max(0, (Math.ceil(win.width / (40 * u)) + 1) * (Math.ceil(win.height / (40 * u)) + 1))
                Rectangle { width: 40 * u; height: 40 * u; color: "transparent"; border.color: "#4060ff"; border.width: 1 }
            }
        }

        // ---------------- Encabezado ----------------
        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 64 * u
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#c00020" }
                GradientStop { position: 0.5; color: "#ff3355" }
                GradientStop { position: 1; color: "#c00020" }
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
                text: Games.count + " JUEGOS"
                font.family: arcadeFont; font.pixelSize: 24 * u; font.bold: true
                color: win.cAccent; style: Text.Outline; styleColor: "#600010"
            }
        }

        // ---------------- Lista de juegos ----------------
        Rectangle {
            id: listPanel
            anchors { top: header.bottom; left: parent.left; bottom: footer.top; margins: 20 * u }
            width: parent.width * 0.42
            color: "#80000020"; border.color: "#3040a0"; border.width: 2 * u; radius: 6 * u

            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 8 * u
                clip: true
                model: Games
                currentIndex: win.current
                highlightMoveDuration: 60
                highlightFollowsCurrentItem: true
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
                    Text {
                        id: num
                        anchors.verticalCenter: parent.verticalCenter
                        x: 10 * u; width: 64 * u
                        text: ("000" + (index + 1)).slice(-3)
                        font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
                        color: sel ? "#300000" : win.cAccent2
                    }
                    Text {
                        anchors { verticalCenter: parent.verticalCenter; left: num.right; right: parent.right; rightMargin: 10 * u }
                        text: title
                        elide: Text.ElideRight
                        font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: sel
                        color: sel ? "black" : win.cText
                    }
                }
            }
            Text {
                anchors.centerIn: parent; visible: Games.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: "NO HAY JUEGOS\n\nCopia tus archivos .zip en:\n" + App.baseDir + "/roms"
                font.family: arcadeFont; font.pixelSize: 18 * u; color: win.cDim; wrapMode: Text.WrapAnywhere
                width: parent.width - 40 * u
            }
        }

        // ---------------- Preview ----------------
        Item {
            anchors { top: header.bottom; left: listPanel.right; right: parent.right; bottom: footer.top; margins: 20 * u }

            Rectangle {
                id: screenFrame
                anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                width: Math.max(0, Math.min(parent.width, (parent.height - info.height - 16 * u) * 4 / 3))
                height: width * 3 / 4
                color: "black"; border.color: win.cAccent; border.width: 4 * u; radius: 4 * u

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
            Text {
                anchors.verticalCenter: parent.verticalCenter; x: 24 * u
                text: "▲▼ ELEGIR   ◄► SALTAR 10   LB/RB LETRA   Ⓐ/✕ JUGAR   Ⓑ/○ OPCIONES"
                font.family: arcadeFont; font.pixelSize: 16 * u; color: win.cText
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
    }

    // =======================================================================
    //  OPCIONES (Ⓑ / Esc en el menú)
    // =======================================================================
    Rectangle {
        id: options
        anchors.fill: parent
        color: "#c0000000"
        visible: win.optionsOpen && !App.gameRunning
        property int index: 0
        readonly property var items: [
            { label: "CONTINUAR", act: function () { win.optionsOpen = false } },
            { label: "SCANLINES: " + (App.scanlines ? "SÍ" : "NO"), act: function () { App.scanlines = !App.scanlines } },
            { label: "FILTRO SUAVE: " + (App.smooth ? "SÍ" : "NO"), act: function () { App.smooth = !App.smooth } },
            { label: "PANTALLA COMPLETA: " + (App.fullscreen ? "SÍ" : "NO"), act: function () { App.fullscreen = !App.fullscreen } },
            { label: "RECARGAR LISTA DE JUEGOS", act: function () { Games.rescan(); win.current = 0; win.optionsOpen = false } },
            { label: "SALIR", act: function () { App.quit() } }
        ]
        function handle(a) {
            if (a === "up") index = (index + items.length - 1) % items.length
            else if (a === "down") index = (index + 1) % items.length
            else if (a === "accept") items[index].act()
            else if (a === "back") win.optionsOpen = false
        }
        Rectangle {
            anchors.centerIn: parent
            width: 560 * u; height: col.height + 60 * u
            color: win.cBg1; border.color: win.cAccent; border.width: 3 * u; radius: 8 * u
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
                            color: index === options.index ? "black" : win.cText
                            font.family: arcadeFont; font.pixelSize: 20 * u; font.bold: true
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
