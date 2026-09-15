#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from pathlib import Path
import unittest
import xml.etree.ElementTree as ElementTree
from xml.etree.ElementTree import Element


ROOT = Path(__file__).resolve().parents[2]
UI_PATH = Path(os.environ.get("RAVEN_DEBUGWINDOW_UI", ROOT / "src/qt/forms/debugwindow.ui"))

DESCRIPTION_WIDGETS = (
    "label_repair_helptext",
    "label_repair_rescan",
    "label_repair_zap1",
    "label2_repair_zap1",
    "label_repair_reindex",
    "label2_repair_reindex",
)

BUTTON_WIDGETS = (
    "btn_rescan",
    "btn_zapwallettxes1",
    "btn_reindex",
)


class DebugWindowRepairLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root: Element = ElementTree.parse(UI_PATH).getroot()
        repair_tab = cls.root.find(".//widget[@name='tab_repair']")
        if repair_tab is None:
            raise AssertionError("debug window has no wallet repair tab")
        cls.repair_tab: Element = repair_tab
        layout = repair_tab.find("layout[@class='QGridLayout']")
        if layout is None:
            raise AssertionError("wallet repair tab has no grid layout")
        cls.layout: Element = layout

    def _widget(self, name):
        widget = self.layout.find(f".//widget[@name='{name}']")
        if widget is None:
            self.fail(f"wallet repair layout is missing {name}")
        return widget

    def test_repair_tab_uses_responsive_layout(self):
        self.assertEqual(self.repair_tab.findall("widget"), [])

        for name in DESCRIPTION_WIDGETS:
            widget = self._widget(name)
            self.assertIsNone(widget.find("property[@name='geometry']"))
            self.assertEqual(
                widget.findtext("property[@name='wordWrap']/bool"),
                "true",
            )

    def test_repair_buttons_keep_their_minimum_width(self):
        for name in BUTTON_WIDGETS:
            widget = self._widget(name)
            self.assertEqual(
                widget.findtext("property[@name='minimumSize']/size/width"),
                "240",
            )


if __name__ == "__main__":
    unittest.main()
