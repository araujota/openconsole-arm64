#!/usr/bin/env python3
"""Acquire Realtek's official UEFI UNDI 2.081 archive via ALTCHA."""
import argparse, base64, hashlib, http.cookiejar, json, urllib.parse, urllib.request

DOWNLOAD_ID = 3413
EXPECTED = "4c4ce542c4aefe4b653e5c7490739e1e942703fe49bd8e0ca2ab80662cc14464"

def fetch(opener, url, data=None, referer=None):
    headers = {"User-Agent": "OpenConsole artifact auditor/1"}
    if referer:
        headers["Referer"] = referer
    with opener.open(urllib.request.Request(url, data=data, headers=headers), timeout=30) as response:
        return response.read()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="destination ZIP")
    args = parser.parse_args()
    opener = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    base = "https://www.realtek.com"
    token = json.loads(fetch(opener, f"{base}/Download/SubmitDownloadRequest?downloadItemid={DOWNLOAD_ID}"))["Data"]
    query = urllib.parse.urlencode({"token": token})
    challenge = json.loads(fetch(opener, f"{base}/Download/GetAltchaChallenge?{query}"))
    number = next((n for n in range(challenge["maxnumber"] + 1)
        if hashlib.sha256((challenge["salt"] + str(n)).encode()).hexdigest()
        == challenge["challenge"]), None)
    if number is None:
        raise SystemExit("ALTCHA solution not found")
    proof = {k: challenge[k] for k in ("algorithm", "challenge", "salt", "signature")}
    proof["number"] = number
    payload = base64.b64encode(json.dumps(proof, separators=(",", ":")).encode()).decode()
    form = urllib.parse.urlencode({"token": token, "altchaPayload": payload}).encode()
    returned_url = json.loads(fetch(opener, f"{base}/Download/CheckDownloadRequestAltcha", form))["Data"]
    endpoint, raw_token = returned_url.split("token=", 1)
    download_url = endpoint + urllib.parse.urlencode({"token": raw_token})
    body = fetch(opener, download_url, referer=base + "/")
    digest = hashlib.sha256(body).hexdigest()
    if digest != EXPECTED:
        raise SystemExit(f"unexpected SHA-256: {digest}")
    with open(args.output, "wb") as stream:
        stream.write(body)
    print(f"wrote {len(body)} bytes; sha256={digest}")

if __name__ == "__main__":
    main()
