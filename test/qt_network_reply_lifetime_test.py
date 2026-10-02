#!/usr/bin/env python3
"""Check that Qt network replies are released after finished handlers run."""

from pathlib import Path
import re
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "src" / "qt" / "ravengui.cpp"
HANDLER = re.compile(
    r"QNetworkAccessManager::finished,\s*this,\s*\[=\]\(QNetworkReply \*reply\)\s*\{(?P<body>.*?)\n\s*\}\n\s*\);",
    re.DOTALL,
)


class QtNetworkReplyLifetimeTest(unittest.TestCase):
    def test_finished_handlers_schedule_reply_deletion(self):
        source = SOURCE.read_text(encoding="utf-8")
        handlers = HANDLER.findall(source)
        self.assertEqual(len(handlers), 2)
        for body in handlers:
            self.assertTrue(body.lstrip().startswith("reply->deleteLater();"))


if __name__ == "__main__":
    unittest.main()
