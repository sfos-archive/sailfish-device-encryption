/*
 * Copyright (c) 2021 Jolla Ltd.
 *
 * License: Proprietary
 */

import QtQuick 2.0
import Sailfish.Silica 1.0

ApplicationWindow {
    allowedOrientations: Orientation.Portrait

    initialPage: Component {
        RestorationPage {
        }
    }
}
