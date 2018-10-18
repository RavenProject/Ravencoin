// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_QT_GUICONSTANTS_H
#define RAVEN_QT_GUICONSTANTS_H

/* Milliseconds between model updates */
static const int MODEL_UPDATE_DELAY = 250;

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024;

/* RavenGUI -- Size of icons in status bar */
static const int STATUSBAR_ICONSIZE = 16;

static const bool DEFAULT_SPLASHSCREEN = true;

/* Invalid field background style */
#define STYLE_INVALID "background:#FF8080; border: 1px solid lightgray; padding: 0px;"
#define STYLE_VALID "border: 1px solid lightgray; padding: 0px;"



/* Transaction list -- unconfirmed transaction */
#define COLOR_UNCONFIRMED QColor(128, 128, 128)
/* Transaction list -- negative amount */
#define COLOR_NEGATIVE QColor(255, 0, 0)
/* Transaction list -- bare address (without label) */
#define COLOR_BAREADDRESS QColor(140, 140, 140)
/* Transaction list -- TX status decoration - open until date */
#define COLOR_TX_STATUS_OPENUNTILDATE QColor(64, 64, 255)
/* Transaction list -- TX status decoration - offline */
#define COLOR_TX_STATUS_OFFLINE QColor(192, 192, 192)
/* Transaction list -- TX status decoration - danger, tx needs attention */
#define COLOR_TX_STATUS_DANGER QColor(200, 100, 100)
/* Transaction list -- TX status decoration - default color */
#define COLOR_BLACK QColor(0, 0, 0)
/* Widget Background color - default color */
#define COLOR_WHITE QColor(255, 255, 255)

/* Background color, very light gray */
#define COLOR_BACKGROUND_LIGHT QColor("#fbfbfe")
/* Ravencoin dark orange */
#define COLOR_DARK_ORANGE QColor(240, 83, 57)
/* Ravencoin light orange */
#define COLOR_LIGHT_ORANGE QColor(247, 148, 51)
/* Ravencoin dark blue */
#define COLOR_DARK_BLUE QColor(46, 62, 128)
/* Ravencoin light blue */
#define COLOR_LIGHT_BLUE QColor(81, 107, 194)
/* Ravencoin asset text */
#define COLOR_ASSET_TEXT QColor(255, 255, 255)
/* Ravencoin shadow color - light mode */
#define COLOR_SHADOW_LIGHT QColor(0, 0, 0, 46)
/* Ravencoin label color */
#define COLOR_LABEL_STRING "color: #4960ad"


/** DARK MODE */
/* Widget background color, dark mode */
#define COLOR_WIDGET_BACKGROUND_DARK QColor("#1c2535")
/* Ravencoin shadow color - dark mode */
#define COLOR_SHADOW_DARK QColor(33,80,181)
/* Ravencoin Light blue - dark mode - dark mode */
#define COLOR_LIGHT_BLUE_DARK QColor("#2b374b")
/* Ravencoin Dark blue - dark mode - dark mode */
#define COLOR_DARK_BLUE_DARK QColor("#1c2535")
/* Pricing widget background color */
#define COLOR_PRICING_WIDGET QColor("#171f2d")








/* Tooltips longer than this (in characters) are converted into rich text,
   so that they can be word-wrapped.
 */
static const int TOOLTIP_WRAP_THRESHOLD = 80;

/* Maximum allowed URI length */
static const int MAX_URI_LENGTH = 255;

/* QRCodeDialog -- size of exported QR Code image */
#define QR_IMAGE_SIZE 300

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

#define QAPP_ORG_NAME "Raven"
#define QAPP_ORG_DOMAIN "raven.org"
#define QAPP_APP_NAME_DEFAULT "Raven-Qt"
#define QAPP_APP_NAME_TESTNET "Raven-Qt-testnet"

#endif // RAVEN_QT_GUICONSTANTS_H
