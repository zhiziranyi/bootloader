"""Static contracts for preserving HTTP response sinks across request setup."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class HttpClientContractTests(unittest.TestCase):
    def test_request_setup_does_not_clear_the_callers_response_sink(self):
        source = (ROOT / "network" / "http_client.c").read_text(encoding="utf-8")
        request_start = source.index("static int32_t http_do_request")
        file_size_start = source.index("int32_t http_get_file_size")
        request_body = source[request_start:file_size_start]

        self.assertNotIn("http_conn_init(conn);", request_body)
        self.assertIn("http_conn_init(&conn);", source[file_size_start:])

    def test_header_terminator_can_span_two_tcp_buffers(self):
        source = (ROOT / "network" / "http_client.c").read_text(encoding="utf-8")

        self.assertIn("bool skip_linefeed;", source)
        self.assertIn("conn->skip_linefeed = true;", source)
        self.assertIn("if (conn->skip_linefeed)", source)
        self.assertIn("data[i] == '\\n'", source)
        self.assertIn("conn->parse_state == PARSE_BODY &&", source)


if __name__ == "__main__":
    unittest.main()
