import re

import requests

from .prompts import SYSTEM_PROMPT


def clamp(value: int, lower: int, upper: int) -> int:
    return max(lower, min(upper, value))


def fallback_regex(text_pt: str) -> str | None:
    t = (text_pt or "").lower().strip()

    # básicos/modos
    if re.search(r"\b(home|in[ií]cio|posi(ç|c)[aã]o inicial)\b", t):
        return "HOME"
    if "ajuda" in t or "help" in t:
        return "HELP"
    if re.search(r"\bdemo\b", t) and re.search(r"\b(on|iniciar|começar|start)\b", t):
        return "DEMO ON"
    if re.search(r"\bdemo\b", t) and re.search(r"\b(off|parar|stop|terminar)\b", t):
        return "DEMO OFF"
    if "cumprimentar" in t:
        return "CUMPRIMENTAR"
    if ("cumprimento" in t or "cumpriment" in t) and re.search(r"\b(on|iniciar|start)\b", t):
        return "CUMPRIMENTO ON"
    if ("cumprimento" in t or "cumpriment" in t) and re.search(r"\b(off|parar|stop)\b", t):
        return "CUMPRIMENTO OFF"

    # garra
    m = re.search(r"\b(abr[ei]r|abr[áa]).*\bgarra\b", t)
    if m:
        m2 = re.search(r"(\d{2,4})\s*(ms|milisseg|mil[ií]s){0,1}", t)
        ms = m2.group(1) if m2 else "300"
        return f"GARRA ABRIR {ms}"

    m = re.search(r"\b(fechar|feche|fech[áa]).*\bgarra\b", t)
    if m:
        m2 = re.search(r"(\d{2,4})\s*(ms|milisseg|mil[ií]s){0,1}", t)
        ms = m2.group(1) if m2 else "400"
        return f"GARRA FECHAR {ms}"

    if re.search(r"\bparar.*\bgarra\b", t):
        return "GARRA PARAR"

    # ordem importa: "desinverter" precisa vir antes de "inverter"
    if "desinverter a garra" in t or "garra normal" in t:
        return "GARRA DIR NORM"
    if "inverter a garra" in t or "garra invertida" in t:
        return "GARRA DIR INV"

    # ação composta deve vir antes de base/cotovelo individuais
    if "pegar" in t or "pega" in t:
        m1 = re.search(r"(?:girar|virar|base)\D*(\d{1,3})", t)
        m2 = re.search(r"(?:cotovelo)\D*(\d{1,3})", t)
        delta = m1.group(1) if m1 else "50"
        cot = m2.group(1) if m2 else "120"
        return f"PEGA {delta} {cot} 400"

    # 180°
    m = re.search(r"\bbase\b.*?(\d{1,3})", t)
    if m:
        return f"BASE {clamp(int(m.group(1)), 0, 180)}"

    m = re.search(r"\b(cotovelo|cotov[êe]lo|elbow)\b.*?(\d{1,3})", t)
    if m:
        return f"COTOVELO {clamp(int(m.group(2)), 0, 180)}"

    # 360° antebraço
    if re.search(r"\bantebra[cç]o\b.*\b(esq(u|ue)rda|esq)\b", t):
        m2 = re.search(r"(\d{2,4})", t)
        ms = m2.group(1) if m2 else "300"
        return f"ANTEBRACO GIRAR ESQ {ms}"

    if re.search(r"\bantebra[cç]o\b.*\b(dir(e|)ita|dir)\b", t):
        m2 = re.search(r"(\d{2,4})", t)
        ms = m2.group(1) if m2 else "300"
        return f"ANTEBRACO GIRAR DIR {ms}"

    if re.search(r"\b(antebra[cç]o).*(parar|stop)\b", t):
        return "ANTEBRACO PARAR"

    # 360° punho
    if re.search(r"\bpunho\b.*\b(esq(u|ue)rda|esq)\b", t):
        m2 = re.search(r"(\d{2,4})", t)
        ms = m2.group(1) if m2 else "300"
        return f"PUNHO GIRAR ESQ {ms}"

    if re.search(r"\bpunho\b.*\b(dir(e|)ita|dir)\b", t):
        m2 = re.search(r"(\d{2,4})", t)
        ms = m2.group(1) if m2 else "300"
        return f"PUNHO GIRAR DIR {ms}"

    if re.search(r"\bpunho.*(parar|stop)\b", t):
        return "PUNHO PARAR"

    return None


def openrouter_map_text_to_command(text: str, api_key: str, model: str) -> str | None:
    try:
        url = "https://openrouter.ai/api/v1/chat/completions"
        headers = {
            "Authorization": f"Bearer {api_key}",
            "HTTP-Referer": "https://localhost",
            "X-Title": "MOVA GUI",
            "Content-Type": "application/json",
        }
        payload = {
            "model": model,
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": text},
            ],
            "temperature": 0.2,
        }
        resp = requests.post(url, headers=headers, json=payload, timeout=25)
        resp.raise_for_status()
        data = resp.json()
        return data["choices"][0]["message"]["content"].strip().splitlines()[0].strip()
    except Exception:
        return None
