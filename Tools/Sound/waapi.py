import sys, json, urllib.request
def call(uri, args=None, options=None):
    body = json.dumps({"uri": uri, "args": args or {}, "options": options or {}}).encode()
    req = urllib.request.Request("http://127.0.0.1:8090/waapi", body, {"Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req))
    except urllib.error.HTTPError as e:
        raise RuntimeError(e.read().decode())
if __name__ == "__main__":
    print(json.dumps(call(sys.argv[1], json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}, json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}), ensure_ascii=False, indent=1))
