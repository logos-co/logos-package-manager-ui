pragma Singleton
import QtQuick

// Module-local icon registry. Mirrors the Logos.Icons pattern from the
// design system — each entry is a Qt.resolvedUrl pointing at an SVG
// next to this file. Consumers use `PackageIcons.pages` instead of
// hand-rolled `Qt.resolvedUrl("../Icons/pages.svg")` strings, so call
// sites stay short and the SVG paths only live in one place.
QtObject {
    readonly property url pages: Qt.resolvedUrl("pages.svg")
}
