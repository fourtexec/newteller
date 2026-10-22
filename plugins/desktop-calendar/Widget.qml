import QtQuick

// Desktop Calendar Widget
// Shows a monthly calendar with the current date highlighted.
Rectangle {
    id: widget

    readonly property var pluginSettings: (parent && parent.modelData && parent.modelData.settings)
                                          ? parent.modelData.settings : ({})
    property bool showWeekNumbers: pluginSettings.showWeekNumbers === true
    property int firstDayOfWeek: pluginSettings.firstDayOfWeek !== undefined
                                 ? Number(pluginSettings.firstDayOfWeek) : 0
    property bool showHeader: pluginSettings.showHeader !== false
    property bool showTodayMarker: pluginSettings.showTodayMarker !== false

    readonly property int currentYear: Shell.now.getFullYear()
    readonly property int currentMonth: Shell.now.getMonth()
    readonly property int today: Shell.now.getDate()

    property int displayYear: currentYear
    property int displayMonth: currentMonth

    readonly property var weekDays: ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    readonly property var monthNames: ["January", "February", "March", "April", "May", "June",
                                       "July", "August", "September", "October", "November", "December"]

    readonly property int daysInMonth: new Date(displayYear, displayMonth + 1, 0).getDate()
    readonly property int firstDay: new Date(displayYear, displayMonth, 1).getDay()
    readonly property int offset: (firstDay - firstDayOfWeek + 7) % 7
    readonly property int prevMonthDays: new Date(displayYear, displayMonth, 0).getDate()
    readonly property int totalCells: offset + daysInMonth
    readonly property int remainingCells: (7 - (totalCells % 7)) % 7
    readonly property int rowCount: Math.ceil(totalCells / 7)

    width: 280
    height: bodyColumn.implicitHeight + 24
    radius: Theme.radius + 4
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.opacity * 0.95)
    border.width: 1
    border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

    // Background drag and middle-click reset (placed beneath interactive controls).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        drag.target: widget
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton) {
                widget.displayYear = widget.currentYear
                widget.displayMonth = widget.currentMonth
            }
        }
    }

    Column {
        id: bodyColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        Item {
            visible: widget.showHeader
            width: parent.width
            height: 36

            Rectangle {
                id: prevBtn
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                radius: Theme.radius
                color: prevMouse.containsMouse
                       ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
                       : "transparent"

                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    source: Shell.tintedIcon("chevron-left", Theme.foreground)
                }

                MouseArea {
                    id: prevMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (widget.displayMonth === 0) {
                            widget.displayMonth = 11
                            widget.displayYear--
                        } else {
                            widget.displayMonth--
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 0

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: widget.monthNames[widget.displayMonth] + " " + widget.displayYear
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 2
                    font.bold: true
                }
            }

            Rectangle {
                id: nextBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                radius: Theme.radius
                color: nextMouse.containsMouse
                       ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
                       : "transparent"

                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    source: Shell.tintedIcon("chevron-right", Theme.foreground)
                }

                MouseArea {
                    id: nextMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (widget.displayMonth === 11) {
                            widget.displayMonth = 0
                            widget.displayYear++
                        } else {
                            widget.displayMonth++
                        }
                    }
                }
            }
        }

        Row {
            width: parent.width
            spacing: 4

            Item {
                visible: widget.showWeekNumbers
                width: 22
                height: 22
            }

            Repeater {
                model: 7
                delegate: Text {
                    required property int index
                    readonly property real gridAvail: bodyColumn.width
                                                      - (widget.showWeekNumbers ? 26 : 0)
                    width: (gridAvail - 24) / 7
                    height: 22
                    text: widget.weekDays[(index + widget.firstDayOfWeek) % 7]
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Row {
            width: parent.width
            spacing: 4

            Column {
                visible: widget.showWeekNumbers
                width: 22
                spacing: 4

                Repeater {
                    model: widget.rowCount
                    delegate: Text {
                        required property int index
                        width: 22
                        height: 30
                        text: String(index + 1)
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: 0.7
                    }
                }
            }

            Grid {
                id: calendarGrid
                readonly property real gridWidth: bodyColumn.width
                                                  - (widget.showWeekNumbers ? 26 : 0)
                readonly property real cellWidth: (gridWidth - 24) / 7
                width: gridWidth
                columns: 7
                spacing: 4

                Repeater {
                    model: widget.offset
                    delegate: Text {
                        required property int index
                        width: calendarGrid.cellWidth
                        height: 30
                        text: String(widget.prevMonthDays - widget.offset + index + 1)
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: 0.45
                    }
                }

                Repeater {
                    model: widget.daysInMonth
                    delegate: Rectangle {
                        id: dayCell
                        required property int index
                        readonly property int dayNumber: index + 1
                        readonly property bool isToday: widget.displayYear === widget.currentYear
                                                        && widget.displayMonth === widget.currentMonth
                                                        && dayNumber === widget.today

                        width: calendarGrid.cellWidth
                        height: 30
                        radius: 6
                        color: widget.showTodayMarker && isToday
                               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                               : (dayArea.containsMouse
                                  ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
                                  : "transparent")
                        border.width: widget.showTodayMarker && isToday ? 1 : 0
                        border.color: Theme.accent

                        Text {
                            anchors.fill: parent
                            text: String(dayCell.dayNumber)
                            color: dayCell.isToday ? Theme.accent : Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                            font.bold: dayCell.isToday
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        MouseArea {
                            id: dayArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }

                Repeater {
                    model: widget.remainingCells
                    delegate: Text {
                        required property int index
                        width: calendarGrid.cellWidth
                        height: 30
                        text: String(index + 1)
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: 0.45
                    }
                }
            }
        }
    }
}
