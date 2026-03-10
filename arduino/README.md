# MOVA Arduino Firmware

Arquivo principal:
- `arduino/MOVA_RobotArm/MOVA_RobotArm.ino`

## Comandos suportados
- `PING`, `HELP`, `HOME`
- `SPEED <1..100>`
- `BASE <0..180>`, `COTOVELO <0..180>`
- `POSE <base> <cotovelo> [vel]`
- `ANTEBRACO GIRAR <DIR|ESQ> [ms]`, `ANTEBRACO PARAR`
- `PUNHO GIRAR <DIR|ESQ> [ms]`, `PUNHO PARAR`
- `GARRA ABRIR [ms]`, `GARRA FECHAR [ms]`, `GARRA PARAR`
- `DIR <BASE|COTOVELO> <INV|NORM>`
- `<ANTEBRACO|PUNHO|GARRA> DIR <INV|NORM>`
- `<ANTEBRACO|PUNHO|GARRA> POTENCIA <0..100>`
- `<ANTEBRACO|PUNHO|GARRA> NEUTRO <us>`
- `<ANTEBRACO|PUNHO|GARRA> RAW <us> [ms]`
- `DEMO ON|OFF`, `CUMPRIMENTO ON|OFF`, `CUMPRIMENTAR`, `PEGA <base> <cotovelo> [ms]`

## Pinos (padrao no sketch)
- `BASE`: D3
- `COTOVELO`: D5
- `ANTEBRACO`: D6
- `PUNHO`: D9
- `GARRA`: D10

## Como subir
1. Abra `MOVA_RobotArm.ino` na IDE Arduino.
2. Selecione a placa e porta corretas.
3. Se necessario, ajuste os pinos no topo do arquivo.
4. Compile e envie.
5. Configure no app Python a mesma baud (`9600` por padrao).

## Observacoes
- Servos 360 usam pulso neutro (`1500us`) e potencia configuravel.
- Se algum servo girar ao contrario, use comando `DIR ... INV`.
- Para servos 360, ajuste `NEUTRO` para parar drift.
