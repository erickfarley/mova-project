# M.O.V.A. - Movimento Orientado por Voz Ativa

## Sobre o projeto
O **M.O.V.A. - Movimento Orientado por Voz Ativa** e um sistema que converte comandos de voz em movimentos de um braco robotico via Arduino.

Ele combina:
- Aplicacao desktop em Python (interface grafica, reconhecimento de voz e mapeamento de comandos).
- Firmware Arduino (recebe comandos pela serial e controla os atuadores/servos).

## Proposito e impacto social
O projeto foi pensado para facilitar a interacao com dispositivos fisicos por comando de voz, com foco em acessibilidade.

Na pratica, ele pode auxiliar pessoas com deficiencia motora em cenarios onde:
- o uso manual de controles e dificil;
- comandos por voz reduzem esforco fisico;
- a automacao de movimentos repetitivos melhora autonomia.

## Como o sistema funciona
Fluxo geral:
1. O usuario fala um comando no app Python.
2. O app converte voz em texto.
3. O texto e mapeado para comando de controle:
4. Prioridade atual: **LLM (OpenRouter)**.
5. Fallback: regex local (se configurado).
6. O comando final e enviado pela serial ao Arduino.
7. O Arduino interpreta e executa o movimento.

## Estrutura principal do repositorio
- `main.py`: ponto de entrada da aplicacao.
- `mova/`: codigo Python modular (UI, mapper, workers, app).
- `arduino/MOVA_RobotArm/MOVA_RobotArm.ino`: firmware do Arduino.
- `.env`: configuracoes de execucao.
- `requirements.txt`: dependencias Python.

## Requisitos
- Python 3.10+ (recomendado 3.13 no ambiente atual).
- Arduino IDE (ou Arduino CLI).
- Placa Arduino compativel com biblioteca Servo.
- Hardware do braco robotico e fonte adequada para os servos.

## Configuracao do Python (app desktop)
No PowerShell, na raiz do projeto:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python main.py
```

## Configuracao do Arduino
Arquivo do firmware:
- `arduino/MOVA_RobotArm/MOVA_RobotArm.ino`

Passos:
1. Abra o arquivo na Arduino IDE.
2. Ajuste os pinos no topo do sketch, se necessario.
3. Selecione placa e porta.
4. Compile e envie para a placa.
5. No app Python, use a mesma `baud` configurada no firmware (`9600` por padrao).

Pinos padrao do sketch:
- BASE: `D3`
- COTOVELO: `D5`
- ANTEBRACO: `D6`
- PUNHO: `D9`
- GARRA: `D10`

## Configuracao do .env
Arquivo: `.env`

Variaveis:
- `OPENROUTER_API_KEY`: chave da API OpenRouter (deixe vazio para sem LLM).
- `OPENROUTER_MODEL`: modelo usado para mapear texto -> comando.
- `MAPPER_PRIORITY`: `LLM` ou `REGEX`.
- `LLM_FALLBACK_REGEX`: `1` para fallback em regex quando LLM falhar, `0` para nao usar fallback.
- `FORCE_SD`: `1` para forcar `sounddevice`; `0` para preferir `PyAudio` quando disponivel.

Exemplo:

```env
OPENROUTER_API_KEY=coloque_sua_chave_aqui
OPENROUTER_MODEL=openrouter/anthropic/claude-3.5-sonnet
MAPPER_PRIORITY=LLM
LLM_FALLBACK_REGEX=1
FORCE_SD=1
```

## Comandos aceitos (resumo)
Comandos enviados ao Arduino incluem:
- `PING`, `HELP`, `HOME`
- `SPEED <vel>`
- `BASE <graus>`, `COTOVELO <graus>`, `POSE <base> <cotovelo> [vel]`
- `ANTEBRACO GIRAR <DIR|ESQ> [ms]`, `ANTEBRACO PARAR`
- `PUNHO GIRAR <DIR|ESQ> [ms]`, `PUNHO PARAR`
- `GARRA ABRIR [ms]`, `GARRA FECHAR [ms]`, `GARRA PARAR`
- `DIR <BASE|COTOVELO> <INV|NORM>`
- `<ANTEBRACO|PUNHO|GARRA> DIR|POTENCIA|NEUTRO|RAW ...`

Comandos adicionais de macro:
- `DEMO ON|OFF`
- `CUMPRIMENTO ON|OFF`
- `CUMPRIMENTAR`
- `PEGA <base> <cotovelo> [ms]`

## Uso basico
1. Ligue o Arduino com firmware gravado.
2. Abra o app (`python main.py`).
3. Clique em **Atualizar Portas** e conecte.
4. Clique em **Iniciar M.O.V.A.**
5. Segure **Falar**, diga o comando e solte.
6. Confira no log o texto ouvido, comando mapeado e resposta do Arduino.

## Solucao de problemas (rapido)
- Import sublinhado na IDE: confirme o interpretador Python da workspace apontando para `.venv`.
- Sem resposta do Arduino: confira porta COM, `baud`, cabo e alimentacao dos servos.
- LLM nao mapeia: verifique `OPENROUTER_API_KEY` no `.env`.
- Voz nao captura: mantenha `FORCE_SD=1` se `PyAudio` nao estiver instalado.

## Aviso de seguranca
Bracos roboticos e servos podem causar danos se montados/energizados incorretamente.
- Sempre teste com limite de movimento seguro.
- Garanta alimentacao adequada e aterramento comum.
- Mantenha distancia das partes moveis durante os testes.
