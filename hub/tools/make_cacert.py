#!/usr/bin/env python3
"""Builds hub/assets/cacert.bin: a small set of root certificates (from the
Mozilla CA store, as packaged by certifi) that covers the vast majority of
HTTPS websites. Parsing all ~150 Mozilla roots for every connection would be
too slow on a 133 MHz DSi, so only the most used roots are kept.

    curl -o cacert.pem https://raw.githubusercontent.com/certifi/python-certifi/master/certifi/cacert.pem
    python3 hub/tools/make_cacert.py cacert.pem

The Mozilla CA certificate store is licensed under the MPL 2.0.
Users can add more roots by placing a cacert.pem in /_nds/TWiLightMenu/hub/.
"""

import os
import re
import sys

WANTED = [
    "ISRG Root X1", "ISRG Root X2",
    "DigiCert Global Root G2", "DigiCert Global Root G3",
    "DigiCert TLS RSA4096 Root G5", "DigiCert TLS ECC P384 Root G5",
    "GlobalSign Root CA - R3", "GlobalSign ECC Root CA - R4", "GlobalSign Root R46",
    "GlobalSign Root E46", "GlobalSign Root CA - R6",
    "GTS Root R1", "GTS Root R3", "GTS Root R4",
    "Amazon Root CA 1", "Amazon Root CA 2", "Amazon Root CA 3", "Amazon Root CA 4",
    "USERTrust RSA Certification Authority", "USERTrust ECC Certification Authority",
    "Sectigo Public Server Authentication Root R46", "Sectigo Public Server Authentication Root E46",
    "Microsoft RSA Root Certificate Authority 2017", "Microsoft ECC Root Certificate Authority 2017",
    "Starfield Root Certificate Authority - G2", "Starfield Services Root Certificate Authority - G2",
    "Go Daddy Root Certificate Authority - G2",
    "SSL.com Root Certification Authority RSA", "SSL.com Root Certification Authority ECC",
    "SSL.com TLS RSA Root CA 2022", "SSL.com TLS ECC Root CA 2022",
    "Certum Trusted Network CA", "Certum Trusted Root CA",
    "HARICA TLS RSA Root CA 2021", "HARICA TLS ECC Root CA 2021",
    "QuoVadis Root CA 2 G3",
]


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "cacert.pem"
    txt = open(src).read()
    blocks = re.findall(r'# Label: "(.*?)"\n.*?(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)', txt, re.S)
    labels = dict(blocks)
    out = []
    for w in WANTED:
        if w in labels:
            out.append(f"# {w}\n{labels[w]}\n")
        else:
            print(f"warning: {w} not found", file=sys.stderr)
    dst = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "cacert.bin")
    # NUL terminated: Mbed TLS requires it for PEM blobs
    open(dst, "w").write("".join(out) + "\0")
    print(f"{len(out)} certificates written to {dst}")


if __name__ == "__main__":
    main()
