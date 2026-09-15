import ipaddress
import socket
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener


_MAX_RESPONSE_BYTES = 10 * 1024 * 1024


class _NoRedirectHandler(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError("URL redirects are not allowed")


def fetch_public_https(url, timeout=30, max_bytes=_MAX_RESPONSE_BYTES):
    parsed = urlparse(url)
    if parsed.scheme != "https" or not parsed.hostname:
        raise ValueError("URL must use https and include a hostname")
    if parsed.username or parsed.password:
        raise ValueError("URL userinfo is not allowed")
    if parsed.port not in (None, 443):
        raise ValueError("URL must use the default HTTPS port")

    try:
        addresses = {
            sockaddr[0]
            for sockaddr in (
                result[4]
                for result in socket.getaddrinfo(
                    parsed.hostname, 443, type=socket.SOCK_STREAM
                )
            )
        }
    except (socket.gaierror, ValueError) as exc:
        raise ValueError("URL host could not be resolved") from exc

    if not addresses:
        raise ValueError("URL host has no usable address")

    for address in addresses:
        ip = ipaddress.ip_address(address)
        if not ip.is_global:
            raise ValueError("URL must resolve to a public address")

    opener = build_opener(_NoRedirectHandler)
    with opener.open(Request(url), timeout=timeout) as response:
        body = response.read(max_bytes + 1)
    if len(body) > max_bytes:
        raise ValueError("URL response exceeds the configured size limit")
    return body
