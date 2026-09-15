from urllib.parse import quote


def rpc_connection_url(user: str, password: str, port: int) -> str:
    """Build a local RPC URL without treating credentials as URI syntax."""
    return "http://{}:{}@127.0.0.1:{}".format(
        quote(user, safe=""),
        quote(password, safe=""),
        port,
    )
