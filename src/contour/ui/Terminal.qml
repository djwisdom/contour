// vim:syntax=qml
import Contour.Terminal
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Window
import Qt5Compat.GraphicalEffects

ContourTerminal
{
    property url bellSoundSource: "qrc:/contour/bell.wav"

    id: vtWidget
    visible: true

    session: terminalSessions.createSession()

    Rectangle {
        id : backgroundColor
        anchors.centerIn: parent
        width:  vtWidget.width
        height:  vtWidget.height
        opacity : session.isImageBackground
            ? session.isBlurBackground ? 1.0 - session.opacityBackground : 1.0
            : 1.0
        color: session.backgroundColor
        visible : true
        focus : false
    }

    Image {
        id: backgroundImage
        width:  vtWidget.width
        height:  vtWidget.height
        opacity : session.opacityBackground
        focus: false
        visible : session.isImageBackground
        source :  session.pathToBackground
    }


    FastBlur {
        visible: session.isBlurBackground
        anchors.fill: backgroundImage
        source: backgroundImage
        radius: 32
    }


    Rectangle {
        anchors.centerIn: parent
        width:  vtWidget.width
        height:  vtWidget.height
        color: session.backgroundColor
        opacity : session.isImageBackground
                ? session.isBlurBackground ? 1.0 - session.opacityBackground : 0.0
                : 0.0
        visible : true
        focus : false
    }


    Rectangle {
        Timer {
            id: sizeWidgetTimer
            interval: 1000;
            running: false;
            onTriggered: sizeWidget.visible = false
        }
        id : sizeWidget
        anchors.centerIn: parent
        border.width: 1
        border.color: "black"
        property int margin: 10
        color: "white"
        visible : false
        focus : false
        Text {
            id : sizeWidgetText
            anchors.centerIn: parent
            font.pointSize: vtWidget.fontSize
            text :  "Size: " + session.pageColumnsCount.toString() + " x " + session.pageLineCount.toString()
        }
    }


    ScrollBar {
        id: vbar
        anchors.top: parent.top
        anchors.right : session.isScrollbarRight ? parent.right : undefined
        anchors.left : session.isScrollbarRight ? undefined : parent.left
        anchors.bottom: parent.bottom
        visible : session.isScrollbarVisible
        orientation: Qt.Vertical
        policy: session.isScrollbarVisible ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        minimumSize : 0.1
        size : vtWidget.session.pageLineCount / (vtWidget.session.pageLineCount + vtWidget.session.historyLineCount)
        stepSize : 1.0 / (vtWidget.session.pageLineCount + vtWidget.session.historyLineCount)
    }

    AudioOutput {
        id: bellAudioOutput
        objectName: "BellAudioOutput"
    }

    MediaPlayer {
        id: bellSoundEffect
        objectName: "Bell"
        source: session.bellSource
        audioOutput: bellAudioOutput
    }

    RequestPermission {
        id: requestFontChangeDialog
        text: "The host application is requesting to change the display font."
        onYesToAllClicked: vtWidget.session.applyPendingFontChange(true, true);
        onYesClicked: vtWidget.session.applyPendingFontChange(true, false);
        onNoToAllClicked: vtWidget.session.applyPendingFontChange(false, true);
        onNoClicked: vtWidget.session.applyPendingFontChange(false, false);
        onRejected: {
            console.log("[Terminal] font change request rejected.", vtWidget.session)
            if (vtWidget.session !== null)
                vtWidget.session.applyPendingFontChange(false, false);
        }
    }


    RequestPermission {
        id: requestLargeFilePaste
        text: "The host application is going to paste large file, are you sure?"
        onYesToAllClicked: vtWidget.session.applyPendingPaste(true, true);
        onYesClicked: vtWidget.session.applyPendingPaste(true, false);
        onNoToAllClicked: vtWidget.session.applyPendingPaste(false, true);
        onNoClicked: vtWidget.session.applyPendingPaste(false, false);
        onRejected: {
            console.log("[Terminal] large file paste is rejected.", vtWidget.session)
            if (vtWidget.session !== null)
                vtWidget.session.applyPendingPaste(false, false);
        }
    }



    RequestPermission {
        id: requestBufferCaptureDialog
        text: "The host application is requesting to capture the terminal buffer."
        onYesToAllClicked: vtWidget.session.executePendingBufferCapture(true, true);
        onYesClicked: vtWidget.session.executePendingBufferCapture(true, false);
        onNoToAllClicked: vtWidget.session.executePendingBufferCapture(false, true);
        onNoClicked: vtWidget.session.executePendingBufferCapture(false, false);
        onRejected: {
            console.log("[Terminal] Buffer capture request rejected.")
            vtWidget.session.executePendingBufferCapture(false, false);
        }
    }

    RequestPermission {
        id: requestShowHostWritableStatusLine
        text: "The host application is requesting to show the host-writable statusline."
        onYesToAllClicked: vtWidget.session.executeShowHostWritableStatusLine(true, true);
        onYesClicked: vtWidget.session.executeShowHostWritableStatusLine(true, false);
        onNoToAllClicked: vtWidget.session.executeShowHostWritableStatusLine(false, true);
        onNoClicked: vtWidget.session.executeShowHostWritableStatusLine(false, false);
        onRejected: vtWidget.session.executeShowHostWritableStatusLine(false, false);
    }

    // Callback, to be invoked whenever the GUI scrollbar has been changed.
    // This will update the VT's viewport respectively.
    function onScrollBarPositionChanged() {
        let vt = vtWidget.session;
        let totalLineCount = (vt.pageLineCount + vt.historyLineCount);
        if(vbar.active)
                vt.scrollOffset = vt.historyLineCount - vbar.position * totalLineCount;
    }

    // Callback to be invoked whenever the VT's viewport is changing.
    // This will update the GUI (vertical) scrollbar respectively.
    function updateScrollBarPosition() {
        let vt = vtWidget.session;
        let totalLineCount = (vt.pageLineCount + vt.historyLineCount);

        vbar.position = (vt.historyLineCount - vt.scrollOffset) / totalLineCount;
    }

    function updateSizeWidget() {
        if (vtWidget.session.upTime > 1.0 && vtWidget.session.showResizeIndicator)
        {
            sizeWidget.visible = true
            sizeWidgetTimer.running = true
            sizeWidgetText.text = "Size: " + vtWidget.session.pageColumnsCount.toString() + " x " + vtWidget.session.pageLineCount.toString()
            sizeWidget.width = sizeWidgetText.contentWidth + sizeWidget.margin
            sizeWidget.height = sizeWidgetText.contentHeight
        }
    }

    onTerminated: {
        console.log("Client process terminated. Closing the window.");
        if (terminalSessions.canCloseWindow())
            Window.window.close(); // https://stackoverflow.com/a/53829662/386670
    }


    function playBell(volume) {
        if (bellSoundEffect.playbackState === MediaPlayer.PlayingState)
           return;

        if (bellSoundEffect.audioOutput)
            // Qt 6 solution to set the volume
            bellSoundEffect.audioOutput.volume = volume;
        else
            // Qt 5 fallback
            bellSoundEffect.volume = volume;

        bellSoundEffect.play()
    }

    function doAlert() {
        Window.window.alert(0);
    }

    function updateFontSize() {
        sizeWidgetText.font.pointSize = vtWidget.session.fontSize
    }

    function onCreateNewTab() {
        terminalSessions.addSession();
    }

    function delay(duration) { // In milliseconds
        var timeStart = new Date().getTime();
        while (new Date().getTime() - timeStart < duration) {
            // Do nothing
        }
    }


    onSessionChanged: (s) => {
        let vt = vtWidget.session;

        // Connect bell control code with an actual sound effect.
        vt.onBell.connect(playBell);

        // Connect alert control of the window
        vt.onAlert.connect(doAlert);

        // Link showNotification signal.
        vt.onShowNotification.connect(vtWidget.showNotification);

        // Link opacityChanged signal.
        vt.onOpacityChanged.connect(vtWidget.opacityChanged);

        // Update the VT's viewport whenever the scrollbar's position changes.
        vbar.onPositionChanged.connect(onScrollBarPositionChanged);

        // Update the scrollbar position whenever the scrollbar size changes, because
        // the position is calculated based on scrollbar's size.
        vbar.onSizeChanged.connect(updateScrollBarPosition);

        // Update the scrollbar's position whenever the VT's viewport changes.
        vt.onScrollOffsetChanged.connect(updateScrollBarPosition);

        // Update font size of elements
        vt.fontSizeChanged.connect(updateFontSize);
        updateFontSize();

        // Show cell-dimensions popup in case of page size changes
        vt.lineCountChanged.connect(updateSizeWidget);
        vt.columnsCountChanged.connect(updateSizeWidget);

        // Permission-wall related hooks.
        vt.requestPermissionForFontChange.connect(requestFontChangeDialog.open);
        vt.requestPermissionForBufferCapture.connect(requestBufferCaptureDialog.open);
        vt.requestPermissionForShowHostWritableStatusLine.connect(requestShowHostWritableStatusLine.open);
        vt.requestPermissionForPasteLargeFile.connect(requestLargeFilePaste.open);
        forceActiveFocus();

    }
}
