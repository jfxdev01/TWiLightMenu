# Converte o pacote de CAs da Mozilla (assets/cacert.pem, de https://curl.se/ca/cacert.pem)
# em ancoras de confianca do BearSSL (arm9/source/ta_gen.c).
#   python tools/gen_ta.py
import base64
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "cacert.pem")
OUT = os.path.join(ROOT, "arm9", "source", "ta_gen.c")

OID_RSA = "1.2.840.113549.1.1.1"
OID_EC = "1.2.840.10045.2.1"
CURVES = {"1.2.840.10045.3.1.7": 23, "1.3.132.0.34": 24, "1.3.132.0.35": 25}


def der_read(b, i):
    """retorna (tag, inicio_conteudo, fim_conteudo, inicio_elemento)"""
    start = i
    tag = b[i]
    i += 1
    ln = b[i]
    i += 1
    if ln & 0x80:
        n = ln & 0x7F
        ln = int.from_bytes(b[i:i + n], "big")
        i += n
    return tag, i, i + ln, start


def children(b, s, e):
    out = []
    i = s
    while i < e:
        t, cs, ce, st = der_read(b, i)
        out.append((t, cs, ce, st))
        i = ce
    return out


def oid_str(b):
    first = b[0]
    parts = [first // 40, first % 40]
    v = 0
    for x in b[1:]:
        v = (v << 7) | (x & 0x7F)
        if not x & 0x80:
            parts.append(v)
            v = 0
    return ".".join(map(str, parts))


def strip0(x):
    while len(x) > 1 and x[0] == 0:
        x = x[1:]
    return x


def parse(der):
    t, s, e, _ = der_read(der, 0)
    cert = children(der, s, e)
    tbs = cert[0]
    f = children(der, tbs[1], tbs[2])
    k = 0
    if f[0][0] == 0xA0:  # version
        k = 1
    # serial, sigalg, issuer, validity, subject, spki
    subj = f[k + 4]
    spki = f[k + 5]
    dn = der[subj[3]:subj[2]]
    alg, bits = children(der, spki[1], spki[2])
    algc = children(der, alg[1], alg[2])
    oid = oid_str(der[algc[0][1]:algc[0][2]])
    key = der[bits[1] + 1:bits[2]]  # pula "unused bits"
    if oid == OID_RSA:
        _, s2, e2, _ = der_read(key, 0)
        n, ex = children(key, s2, e2)[:2]
        return dn, ("rsa", strip0(key[n[1]:n[2]]), strip0(key[ex[1]:ex[2]]))
    if oid == OID_EC:
        curve = CURVES.get(oid_str(der[algc[1][1]:algc[1][2]]))
        if curve is None:
            return None
        return dn, ("ec", curve, key)
    return None


def carr(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("\t" + ", ".join(f"0x{x:02X}" for x in data[i:i + 16]) + ",")
    return f"static const unsigned char {name}[] = {{\n" + "\n".join(lines) + "\n};\n"


def main():
    pem = open(SRC, encoding="utf-8").read()
    blocks = re.findall(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem, re.S)
    out = ["// gerado por tools/gen_ta.py a partir do pacote de CAs da Mozilla — nao editar", '#include "bearssl.h"', ""]
    entries = []
    for i, b in enumerate(blocks):
        der = base64.b64decode("".join(b.split()))
        r = parse(der)
        if not r:
            continue
        dn, key = r
        out.append(carr(f"TA{i}_DN", dn))
        if key[0] == "rsa":
            out.append(carr(f"TA{i}_N", key[1]))
            out.append(carr(f"TA{i}_E", key[2]))
            entries.append(
                f"\t{{{{(unsigned char*)TA{i}_DN, sizeof TA{i}_DN}}, BR_X509_TA_CA, {{BR_KEYTYPE_RSA, {{.rsa = {{(unsigned char*)TA{i}_N, sizeof TA{i}_N, (unsigned char*)TA{i}_E, sizeof TA{i}_E}}}}}}}},")
        else:
            out.append(carr(f"TA{i}_Q", key[2]))
            entries.append(
                f"\t{{{{(unsigned char*)TA{i}_DN, sizeof TA{i}_DN}}, BR_X509_TA_CA, {{BR_KEYTYPE_EC, {{.ec = {{{key[1]}, (unsigned char*)TA{i}_Q, sizeof TA{i}_Q}}}}}}}},")
    out.append("const br_x509_trust_anchor g_trustAnchors[] = {")
    out += entries
    out.append("};")
    out.append(f"const unsigned g_trustAnchorsNum = {len(entries)};")
    open(OUT, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print(f"{len(entries)} ancoras de confianca -> {OUT}")


if __name__ == "__main__":
    main()
