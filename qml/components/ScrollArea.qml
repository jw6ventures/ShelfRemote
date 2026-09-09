import QtQuick
import QtQuick.Window
import ShelfRemote

// A Flickable that works from a remote. A plain Flickable only scrolls by mouse
// or touch: focus moving to a control below the fold left it invisible, and
// non-focusable content (a long description, a chapter list) could not be reached
// at all. This one scrolls the focused control back into view and lets Up/Down
// scroll whatever nothing else consumed.
Flickable {
    id: view

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    // A short screen is the only case that scrolls; leave the rest inert so a
    // stray drag can't shift content that already fits.
    interactive: contentHeight > height

    readonly property Item focusedItem: view.Window.activeFocusItem
    onFocusedItemChanged: view.ensureVisible(view.focusedItem)
    onContentHeightChanged: view.ensureVisible(view.focusedItem)

    // True when `item` is one of our own descendants: several ScrollAreas can be
    // alive at once (one per stacked screen) and only the one holding the focused
    // control should move. (Not named contains(): Item already has that.)
    function ownsItem(item) {
        for (var p = item; p; p = p.parent)
            if (p === view.contentItem) return true;
        return false;
    }

    function ensureVisible(item) {
        if (!item || contentHeight <= height || !ownsItem(item))
            return;
        var top = item.mapToItem(view.contentItem, 0, 0).y;
        var bottom = top + item.height;
        var maxY = contentHeight - height;
        if (top - Theme.spacing < contentY)
            contentY = Math.max(0, Math.min(maxY, top - Theme.spacing));
        else if (bottom + Theme.spacing > contentY + height)
            contentY = Math.max(0, Math.min(maxY, bottom + Theme.spacing - height));
    }

    // Half a page per press: enough to make progress, small enough to keep the
    // reader's place. Returns false when there is nothing left to scroll so the
    // key keeps travelling (Up/Down still belong to the screen behind us).
    function scrollByPixels(delta) {
        if (contentHeight <= height)
            return false;
        var target = Math.max(0, Math.min(contentHeight - height, contentY + delta));
        if (target === contentY)
            return false;
        contentY = target;
        return true;
    }

    Keys.onUpPressed: function(event) { event.accepted = view.scrollByPixels(-view.height / 2); }
    Keys.onDownPressed: function(event) { event.accepted = view.scrollByPixels(view.height / 2); }
}
